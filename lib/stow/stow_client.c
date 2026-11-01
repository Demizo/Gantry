/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Stow client
 *
 * @details A session runtime instance serves every peer. On its thread the client verifies peers, sends every request,
 * matches responses by tag and fails requests that time out or outlive their session.
 *
 * @author Demizo (demizodemazo@gmail.com)
 *
 */

#include <gantry/error.h>
#include <gantry/memory.h>
#include <gantry/stow/stow_client.h>
#include <gantry/stow/stow_msg.h>
#include <gantry/stow/stow_runtime.h>
#include <gantry/stow/stow_transport.h>
#include <stddef.h>
#include <string.h>
#include <sys/errno.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net_buf.h>

#include "stow_client_internal.h"

/**
 * @brief Logger for module
 */
LOG_MODULE_REGISTER(stow_client, CONFIG_STOW_CLIENT_LOG_LEVEL);

//**********************************************************
//* Local Definitions
//**********************************************************

/**
 * @brief Work the client thread can have queued: a send and a response per pending request, plus room for Updates
 */
#define QUEUE_SIZE (CONFIG_STOW_CLIENT_MAX_PEERS * (2 * CONFIG_STOW_CLIENT_MAX_PENDING + 4))

//**********************************************************
//* Typedefs, Enums, and Structs
//**********************************************************

/**
 * @brief A Get, Set, Subscribe or Unsubscribe request
 */
struct request
{
    enum stow_message_code code; /**< Message code */
    const uint16_t* ids;         /**< Generated ID of each item */
    const data_value_t* values;  /**< Value of each item for a Set, otherwise NULL */
    size_t count;                /**< Number of items */
};

//**********************************************************
//* Static Function Declarations
//**********************************************************

static void update_event_on_free(event_t* event);
static void result_event_on_free(event_t* event);

static int encode_request(
    struct stow_client_peer* peer, struct stow_transport* transport, uint8_t tag, const struct request* req,
    struct net_buf** buf_out);
static int check_request(const struct stow_client_schema* schema, const struct request* req);
static int check_available(struct stow_client_peer* peer, const struct request* req);
static void abandon_request(struct stow_client_peer* peer, void* ids, event_t* result, bool waited);
static int request(
    struct stow_client_peer* peer, const struct request* req, stow_client_cb_t cb, void* user_data,
    struct sync_call* sync);
static int request_sync(struct stow_client_peer* peer, const struct request* req, struct stow_client_value* values_out);

static int resolve_item(const struct stow_msg_items* items, struct stow_msg_entry* entry);
static void release_values(struct stow_client_peer* peer, struct stow_client_value* values, size_t count);
static void complete_get(
    struct stow_client_peer* peer, struct slot* slot, struct stow_msg_reader* reader, const struct stow_msg* msg);
static void complete_request(
    struct stow_client_peer* peer, struct slot* slot, int err, struct stow_msg_reader* reader,
    const struct stow_msg* msg);
static void handle_update(struct stow_client_peer* peer, struct stow_msg_reader* reader);

static int accept_peer(struct stow_session* session);
static void open_peer(struct stow_session* session);
static void close_peer(struct stow_session* session);
static void handle_rx(struct stow_session* session, const struct net_buf* buf);
static void send_request(struct stow_session* session, uint32_t seq, struct net_buf* buf);
static k_timeout_t expire_requests(void);
static void free_items(struct stow_session* session);
static int start_client(void);

//**********************************************************
//* Static Variable Definitions
//**********************************************************

/**
 * @brief Define the Update event
 */
DEFINE_EVENT_TYPE(EVENT_ID_STOW_CLIENT_UPDATE, stow_client_update_event, update_event_on_free);

/**
 * @brief Define the result event
 */
DEFINE_EVENT_TYPE(EVENT_ID_STOW_CLIENT_RESULT, stow_client_result_event, result_event_on_free);

/**
 * @brief Peer slots
 */
static struct stow_client_peer peers[CONFIG_STOW_CLIENT_MAX_PEERS];

/**
 * @brief What the client does with its sessions
 */
static const struct stow_runtime_role client_role = {
    .accept = accept_peer,
    .opened = open_peer,
    .closed = close_peer,
    .receive = handle_rx,
    .work = send_request,
    .idle = expire_requests,
    .freed = free_items,
};

/**
 * @brief The client's session runtime instance and thread
 */
STOW_RUNTIME_DEFINE(
    stow_client, client_role, peers, session, QUEUE_SIZE, CONFIG_STOW_CLIENT_THREAD_STACK_SIZE,
    CONFIG_STOW_CLIENT_THREAD_PRIORITY);

/**
 * @brief Start the client at boot
 */
SYS_INIT(start_client, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

//**********************************************************
//* Static Function Definitions
//**********************************************************

/**
 * @brief Encode a request
 *
 * @return SUCCESS, -EBUSY when no buffer is free, -EMSGSIZE when the request does not fit, or a codec's error
 * @acquires net_buf buf_out on-success
 */
static int encode_request(
    struct stow_client_peer* peer, struct stow_transport* transport, uint8_t tag, const struct request* req,
    struct net_buf** buf_out)
{
    struct net_buf* buf = NULL;
    size_t budget = 0;
    int ret = stow_transport_alloc(transport, peer->info.max_message, &buf, &budget);
    if (ret != SUCCESS)
    {
        return ret;
    }

    struct stow_msg_writer writer;
    stow_msg_writer_init(&writer, net_buf_tail(buf), budget);
    (void)stow_msg_write(&writer, &(struct stow_msg){ .code = req->code, .tag = tag, .count = (uint16_t)req->count });
    for (size_t i = 0; i < req->count; i++)
    {
        const struct stow_msg_entry entry = {
            .id = peer->items[req->ids[i]].remote_id,
            .codec = peer_item(peer, req->ids[i])->interface,
            .value = (req->values != NULL) ? req->values[i] : (data_value_t){ 0 },
        };
        (void)stow_msg_put_entry(&writer, &entry);
    }

    size_t len = 0;
    ret = stow_msg_finish(&writer, &len);
    if (ret != SUCCESS)
    {
        net_buf_unref(buf);
        return (ret == -E2BIG) ? -EMSGSIZE : ret;
    }
    net_buf_add(buf, len);
    *buf_out = buf;
    PASS_OWNERSHIP(buf);
    return SUCCESS;
}

/**
 * @brief Check a request's IDs, and a Set's values, against the schema
 *
 * @return SUCCESS, or -EINVAL for an unknown ID or an invalid value
 */
static int check_request(const struct stow_client_schema* schema, const struct request* req)
{
    for (size_t i = 0; i < req->count; i++)
    {
        if (req->ids[i] >= schema->item_count)
        {
            return -EINVAL;
        }
        const struct stow_client_item* item = &schema->items[req->ids[i]];
        if (req->code == STOW_MSG_SET &&
            (req->values[i].type != item->type || !item->interface->validate(&item->constraints, req->values[i])))
        {
            return -EINVAL;
        }
    }
    return SUCCESS;
}

/**
 * @brief Check that the peer is connected and has every item
 *
 * @note Call with the peer locked.
 *
 * @return SUCCESS, -ENOTCONN, or -ENOTSUP for an item the peer lacks
 */
static int check_available(struct stow_client_peer* peer, const struct request* req)
{
    if (peer->state != PEER_READY)
    {
        return -ENOTCONN;
    }
    for (size_t i = 0; i < req->count; i++)
    {
        if (peer->items[req->ids[i]].availability != STOW_CLIENT_AVAILABLE)
        {
            return -ENOTSUP;
        }
    }
    return SUCCESS;
}

/**
 * @brief Release what a request that never got a slot holds
 *
 * @param peer The peer
 * @param ids The request's copy of its item IDs
 * @param result The result event of an asynchronous request, otherwise NULL
 * @param waited Whether the caller took a slot-free wakeup, which is passed on to the next waiter
 * @releases memory ids
 * @releases memory result
 */
static void abandon_request(struct stow_client_peer* peer, void* ids, event_t* result, bool waited)
{
    MEM_UNREF(&ids);
    EVENT_UNREF(&result);
    if (waited)
    {
        k_sem_give(&peer->slot_free);
    }
}

/**
 * @brief Queue a request for the client thread to send
 *
 * @details A blocking caller waits for a free slot and tag; an asynchronous one gets -EAGAIN.
 *
 * @return SUCCESS when the request will finish through @p cb or @p sync, otherwise why it was not sent
 */
static int request(
    struct stow_client_peer* peer, const struct request* req, stow_client_cb_t cb, void* user_data,
    struct sync_call* sync)
{
    if (peer == NULL || req->ids == NULL || req->count == 0U || req->count > UINT16_MAX ||
        (req->code == STOW_MSG_SET && req->values == NULL) || (cb == NULL && sync == NULL))
    {
        return -EINVAL;
    }
    int ret = check_request(peer_endpoint(peer)->schema, req);
    if (ret != SUCCESS)
    {
        return ret;
    }

    event_t* result = NULL;
    if (cb != NULL)
    {
        size_t values = (req->code == STOW_MSG_GET) ? req->count : 0U;
        if (EVENT_ALLOC(
                &stow_client_result_event,
                sizeof(struct stow_client_result_payload) + values * sizeof(struct stow_client_value),
                &result) != SUCCESS)
        {
            return -ENOBUFS;
        }
        struct stow_client_peer* held = stow_client_peer_ref(peer);
        *(struct stow_client_result_payload*)result->data.buf = (struct stow_client_result_payload){
            .peer = held,
            .index = STOW_CLIENT_NO_INDEX,
        };
        PASS_OWNERSHIP(held);
    }

    void* ids_copy = NULL;
    if (req->code == STOW_MSG_GET)
    {
        if (MEM_ALLOC(req->count * sizeof(uint16_t), &ids_copy) != SUCCESS)
        {
            EVENT_UNREF(&result);
            return -ENOBUFS;
        }
        memcpy(ids_copy, req->ids, req->count * sizeof(uint16_t));
    }

    const struct slot pending = {
        .code = req->code,
        .count = (uint16_t)req->count,
        .ids = ids_copy,
        .cb = cb,
        .user_data = user_data,
        .event = result,
        .sync = sync,
    };
    struct slot* slot = NULL;
    struct stow_transport* transport = NULL;
    uint32_t seq = 0;
    bool waited = false;
    while (true)
    {
        int64_t retry_at = INT64_MAX;
        k_spinlock_key_t key = k_spin_lock(&peer->lock);
        ret = check_available(peer, req);
        if (ret != SUCCESS)
        {
            k_spin_unlock(&peer->lock, key);
            abandon_request(peer, ids_copy, result, waited);
            return ret;
        }
        slot = z_stow_client_slot_take(peer, &pending, &retry_at);
        transport = peer->session.transport;
        seq = (slot != NULL) ? slot->seq : 0U;
        bool more_free = z_stow_client_slot_any_free(peer);
        k_spin_unlock(&peer->lock, key);

        if (slot != NULL)
        {
            if (more_free)
            {
                k_sem_give(&peer->slot_free);
            }
            break;
        }
        if (sync == NULL)
        {
            abandon_request(peer, ids_copy, result, waited);
            return -EAGAIN;
        }
        k_timeout_t wait = (retry_at == INT64_MAX) ? K_FOREVER : K_MSEC(MAX(retry_at - k_uptime_get(), 0));
        (void)k_sem_take(&peer->slot_free, wait);
        waited = true;
    }
    PASS_OWNERSHIP(ids_copy);
    PASS_OWNERSHIP(result);

    struct net_buf* buf = NULL;
    ret = encode_request(peer, transport, slot->tag, req, &buf);
    if (ret != SUCCESS)
    {
        return z_stow_client_slot_cancel(peer, slot, seq) ? ret : SUCCESS;
    }

    ret = stow_runtime_post(&stow_client, peer->session.endpoint, peer->session.id, seq, buf);
    if (ret != SUCCESS)
    {
        return z_stow_client_slot_cancel(peer, slot, seq) ? ((ret == -ENOBUFS) ? -EBUSY : ret) : SUCCESS;
    }
    PASS_OWNERSHIP(slot);
    return SUCCESS;
}

/**
 * @brief Send a request and block until it finishes
 *
 * @param peer The peer
 * @param req The request
 * @param values_out Where a Get's values go, otherwise NULL
 */
static int request_sync(struct stow_client_peer* peer, const struct request* req, struct stow_client_value* values_out)
{
    if (k_is_in_isr() || stow_runtime_in_thread(&stow_client))
    {
        ASSERT(false, "Blocking Stow client calls are not allowed here");
        return -EWOULDBLOCK;
    }

    struct sync_call sync = { .values = values_out };
    k_sem_init(&sync.done, 0, 1);

    int ret = request(peer, req, NULL, NULL, &sync);
    if (ret != SUCCESS)
    {
        return ret;
    }
    (void)k_sem_take(&sync.done, K_FOREVER);
    return sync.err;
}

/**
 * @brief Resolver for a peer's messages: a wire item ID is the available item that peer's Describe gave that ID
 */
static int resolve_item(const struct stow_msg_items* items, struct stow_msg_entry* entry)
{
    const struct stow_client_peer* peer = CONTAINER_OF(items, struct stow_client_peer, resolver);
    for (uint16_t id = 0; id < peer_item_count(peer); id++)
    {
        if (peer->items[id].availability == STOW_CLIENT_AVAILABLE && peer->items[id].remote_id == entry->id)
        {
            entry->local = id;
            entry->codec = peer_item(peer, id)->interface;
            return SUCCESS;
        }
    }
    return -ENOENT;
}

/**
 * @brief Release decoded values
 *
 * @releases memory values
 */
static void release_values(struct stow_client_peer* peer, struct stow_client_value* values, size_t count)
{
    for (size_t i = 0; i < count; i++)
    {
        struct stow_client_value* value = &values[i];
        stow_client_release(peer_endpoint(peer), value);
    }
}

/**
 * @brief Finish a Get with the values in its response, which must name the requested items in order
 *
 * @releases slot slot
 */
static void complete_get(
    struct stow_client_peer* peer, struct slot* slot, struct stow_msg_reader* reader, const struct stow_msg* msg)
{
    struct stow_client_result_payload* result =
        (slot->event != NULL) ? (struct stow_client_result_payload*)slot->event->data.buf : NULL;
    struct stow_client_value* values = (result != NULL) ? result->values : slot->sync->values;
    if (msg->count != slot->count)
    {
        z_stow_client_slot_fail(peer, slot, -ENOMSG);
        return;
    }

    for (uint16_t i = 0; i < msg->count; i++)
    {
        struct stow_msg_entry entry;
        int ret = stow_msg_next_entry(reader, &entry);
        if (ret == SUCCESS)
        {
            if (entry.local == slot->ids[i])
            {
                values[i] = (struct stow_client_value){ .id = entry.local, .value = entry.value };
                PASS_OWNERSHIP(entry);
                continue;
            }
            stow_msg_entry_release(&entry);
            ret = -ENOMSG;
        }
        release_values(peer, values, i);
        z_stow_client_slot_fail(peer, slot, (ret == -ENOMEM) ? -ENOBUFS : -ENOMSG);
        return;
    }

    if (result != NULL)
    {
        result->count = msg->count;
    }
    z_stow_client_slot_finish(peer, slot, SUCCESS, STOW_CLIENT_NO_INDEX);
}

/**
 * @brief Finish an application request with its response, or with why the response could not be read
 *
 * @releases slot slot
 */
static void complete_request(
    struct stow_client_peer* peer, struct slot* slot, int err, struct stow_msg_reader* reader,
    const struct stow_msg* msg)
{
    if (err != SUCCESS)
    {
        z_stow_client_slot_fail(peer, slot, err);
        return;
    }
    if (msg->code == STOW_MSG_ERROR)
    {
        uint16_t index = (msg->error.index < slot->count) ? (uint16_t)msg->error.index : STOW_CLIENT_NO_INDEX;
        z_stow_client_slot_finish(peer, slot, msg->error.err, index);
        return;
    }
    if (msg->code != stow_msg_answer(slot->code))
    {
        z_stow_client_slot_fail(peer, slot, -ENOMSG);
        return;
    }
    if (msg->code == STOW_MSG_GET_RESPONSE)
    {
        complete_get(peer, slot, reader, msg);
        return;
    }
    z_stow_client_slot_finish(peer, slot, SUCCESS, STOW_CLIENT_NO_INDEX);
}

static void handle_update(struct stow_client_peer* peer, struct stow_msg_reader* reader)
{
    if (peer->state != PEER_READY)
    {
        return;
    }

    struct stow_msg_entry entry;
    int ret = stow_msg_next_entry(reader, &entry);
    if (ret != SUCCESS)
    {
        LOG_WRN("Dropped an Update from session %u (%d)", peer->session.id, ret);
        return;
    }

    event_t* event = NULL;
    if (EVENT_ALLOC(&stow_client_update_event, sizeof(struct stow_client_update_payload), &event) != SUCCESS)
    {
        LOG_WRN("Dropped an Update from session %u, no event was free", peer->session.id);
        stow_msg_entry_release(&entry);
        return;
    }
    struct stow_client_peer* held = stow_client_peer_ref(peer);
    *(struct stow_client_update_payload*)event->data.buf = (struct stow_client_update_payload){
        .peer = held,
        .value = { .id = entry.local, .value = entry.value },
    };
    PASS_OWNERSHIP(held);
    PASS_OWNERSHIP(entry);

    CALL_HOOKS(update, event);
    EVENT_UNREF(&event);
}

/**
 * @brief Release the value of an Update event and its reference to the peer
 */
static void update_event_on_free(event_t* event)
{
    struct stow_client_update_payload* update = (struct stow_client_update_payload*)event->data.buf;
    struct stow_client_value value = update->value;
    struct stow_client_peer* peer = update->peer;
    stow_client_release(peer_endpoint(peer), &value);
    stow_client_peer_unref(&peer);
}

/**
 * @brief Release the values of a result event and its reference to the peer
 */
static void result_event_on_free(event_t* event)
{
    struct stow_client_result_payload* result = (struct stow_client_result_payload*)event->data.buf;
    struct stow_client_peer* peer = result->peer;
    for (uint16_t i = 0; i < result->count; i++)
    {
        struct stow_client_value value = result->values[i];
        stow_client_release(peer_endpoint(peer), &value);
    }
    stow_client_peer_unref(&peer);
}

/**
 * @brief Check a session's endpoint and give the peer its item table
 *
 * @return SUCCESS, -EINVAL for an empty schema, or -ENOMEM
 */
static int accept_peer(struct stow_session* session)
{
    struct stow_client_peer* peer = CONTAINER_OF(session, struct stow_client_peer, session);
    const struct stow_client_endpoint* client = peer_endpoint(peer);
    if (client->schema == NULL || client->schema->item_count == 0U)
    {
        return -EINVAL;
    }

    void* items = NULL;
    if (MEM_ALLOC(client->schema->item_count * sizeof(struct item_state), &items) != SUCCESS)
    {
        return -ENOMEM;
    }
    peer->timeout_ms = (client->timeout_ms != 0U) ? client->timeout_ms : CONFIG_STOW_CLIENT_TIMEOUT_MS;
    peer->items = items;
    peer->resolver = (struct stow_msg_items){ .resolve = resolve_item };
    PASS_OWNERSHIP(items);
    return SUCCESS;
}

static void open_peer(struct stow_session* session)
{
    z_stow_client_verify_start(CONTAINER_OF(session, struct stow_client_peer, session));
}

/**
 * @brief Fail every pending request, then report the close through the hooks
 */
static void close_peer(struct stow_session* session)
{
    struct stow_client_peer* peer = CONTAINER_OF(session, struct stow_client_peer, session);
    enum peer_state previous = peer->state;

    k_spinlock_key_t key = k_spin_lock(&peer->lock);
    peer->state = PEER_CLOSED;
    k_spin_unlock(&peer->lock, key);

    z_stow_client_slot_fail_all(peer);

    key = k_spin_lock(&peer->lock);
    peer->slots.quarantined = 0U;
    k_spin_unlock(&peer->lock, key);
    k_sem_give(&peer->slot_free);

    if (previous == PEER_VERIFYING)
    {
        CALL_HOOKS(connected, peer, -ENOTCONN);
    }
    else if (previous == PEER_READY)
    {
        CALL_HOOKS(disconnected, peer);
    }
}

static void handle_rx(struct stow_session* session, const struct net_buf* buf)
{
    struct stow_client_peer* peer = CONTAINER_OF(session, struct stow_client_peer, session);
    if (peer->state != PEER_VERIFYING && peer->state != PEER_READY)
    {
        LOG_WRN("Dropping a message for session %u, whose peer failed verification", session->id);
        return;
    }

    struct stow_msg_reader reader;
    struct stow_msg msg;
    int ret = stow_msg_read(&reader, buf->data, buf->len, &peer->resolver, &msg);
    if (ret == SUCCESS && msg.code == STOW_MSG_UPDATE)
    {
        handle_update(peer, &reader);
        return;
    }
    if (msg.tag == STOW_TAG_NONE)
    {
        LOG_WRN("Dropped a message without a tag from session %u (%d)", session->id, ret);
        return;
    }

    struct slot* slot = z_stow_client_slot_find_sent(peer, msg.tag);
    if (slot == NULL)
    {
        bool late = z_stow_client_tag_answered(peer, msg.tag);
        LOG_WRN(
            "Dropped %s response with tag %u from session %u", late ? "a late" : "an unexpected", msg.tag, session->id);
        return;
    }

    /* A response that cannot be read still answers the request its tag names */
    int err = (ret == SUCCESS) ? SUCCESS : (ret == -ENOMEM) ? -ENOBUFS : -ENOMSG;
    if (!slot->internal)
    {
        complete_request(peer, slot, err, &reader, &msg);
        return;
    }

    enum stow_message_code request_code = slot->code;
    z_stow_client_slot_finish(peer, slot, SUCCESS, STOW_CLIENT_NO_INDEX);
    z_stow_client_verify_response(peer, request_code, (ret == -EPROTONOSUPPORT) ? ret : err, &reader, &msg);
}

/**
 * @brief Send a queued request, unless its slot was cancelled or failed
 *
 * @param session The peer's session
 * @param seq Sequence number the slot had when the request was queued
 * @param buf The request
 */
static void send_request(struct stow_session* session, uint32_t seq, struct net_buf* buf)
{
    struct stow_client_peer* peer = CONTAINER_OF(session, struct stow_client_peer, session);
    k_spinlock_key_t key = k_spin_lock(&peer->lock);
    struct slot* slot = z_stow_client_slot_find_queued(peer, seq);
    k_spin_unlock(&peer->lock, key);
    if (slot == NULL)
    {
        return;
    }

    struct net_buf* sent = net_buf_ref(buf);
    (void)z_stow_client_slot_send(peer, slot, sent);
}

/**
 * @brief Fail every sent request whose time is up, and a verification that timed out
 *
 * @return Time until the next sent request times out
 */
static k_timeout_t expire_requests(void)
{
    int64_t now = k_uptime_get();
    int64_t deadline = INT64_MAX;
    for (size_t p = 0; p < ARRAY_SIZE(peers); p++)
    {
        struct stow_client_peer* peer = &peers[p];
        if (z_stow_client_slots_expire(peer, now))
        {
            z_stow_client_verify_failed(peer, -ETIMEDOUT);
        }
        deadline = MIN(deadline, z_stow_client_slots_deadline(peer));
    }
    return (deadline == INT64_MAX) ? K_FOREVER : K_MSEC(MAX(deadline - k_uptime_get(), 0));
}

static void free_items(struct stow_session* session)
{
    struct stow_client_peer* peer = CONTAINER_OF(session, struct stow_client_peer, session);
    void* items = peer->items;
    peer->items = NULL;
    MEM_UNREF(&items);
}

static int start_client(void)
{
    for (size_t i = 0; i < ARRAY_SIZE(peers); i++)
    {
        k_sem_init(&peers[i].slot_free, 0, 1);
    }
    return stow_runtime_start(&stow_client);
}

//**********************************************************
//* Public Function Definitions
//**********************************************************

struct stow_client_peer* stow_client_peer_ref(struct stow_client_peer* peer)
{
    struct stow_session* session = stow_session_ref(&peer->session);
    struct stow_client_peer* held = CONTAINER_OF(session, struct stow_client_peer, session);
    PASS_OWNERSHIP(session);
    return held;
}

void stow_client_peer_unref(struct stow_client_peer** peer)
{
    struct stow_client_peer* released = *peer;
    *peer = NULL;
    if (released != NULL)
    {
        struct stow_session* session = &released->session;
        stow_session_unref(&session);
    }
}

const struct stow_client_endpoint* stow_client_peer_endpoint(struct stow_client_peer* peer)
{
    return peer_endpoint(peer);
}

uint32_t stow_client_peer_session_id(struct stow_client_peer* peer) { return peer->session.id; }

int stow_client_peer_info(struct stow_client_peer* peer, struct stow_client_peer_info* info_out)
{
    k_spinlock_key_t key = k_spin_lock(&peer->lock);
    bool ready = (peer->state == PEER_READY);
    if (ready)
    {
        *info_out = peer->info;
    }
    k_spin_unlock(&peer->lock, key);
    return ready ? SUCCESS : -ENOTCONN;
}

enum stow_client_availability stow_client_availability(struct stow_client_peer* peer, uint16_t id)
{
    k_spinlock_key_t key = k_spin_lock(&peer->lock);
    enum stow_client_availability availability = STOW_CLIENT_MISSING;
    if (peer->state == PEER_READY && id < peer_item_count(peer))
    {
        availability = peer->items[id].availability;
    }
    k_spin_unlock(&peer->lock, key);
    return availability;
}

int stow_client_get(
    struct stow_client_peer* peer, const uint16_t* ids, size_t count, struct stow_client_value* values_out)
{
    if (values_out == NULL)
    {
        return -EINVAL;
    }
    return request_sync(peer, &(struct request){ .code = STOW_MSG_GET, .ids = ids, .count = count }, values_out);
}

int stow_client_set(struct stow_client_peer* peer, const uint16_t* ids, const data_value_t* values, size_t count)
{
    return request_sync(
        peer, &(struct request){ .code = STOW_MSG_SET, .ids = ids, .values = values, .count = count }, NULL);
}

int stow_client_subscribe(struct stow_client_peer* peer, const uint16_t* ids, size_t count)
{
    return request_sync(peer, &(struct request){ .code = STOW_MSG_SUBSCRIBE, .ids = ids, .count = count }, NULL);
}

int stow_client_unsubscribe(struct stow_client_peer* peer, const uint16_t* ids, size_t count)
{
    return request_sync(peer, &(struct request){ .code = STOW_MSG_UNSUBSCRIBE, .ids = ids, .count = count }, NULL);
}

int stow_client_get_async(
    struct stow_client_peer* peer, const uint16_t* ids, size_t count, stow_client_cb_t cb, void* user_data)
{
    return request(peer, &(struct request){ .code = STOW_MSG_GET, .ids = ids, .count = count }, cb, user_data, NULL);
}

int stow_client_set_async(
    struct stow_client_peer* peer, const uint16_t* ids, const data_value_t* values, size_t count, stow_client_cb_t cb,
    void* user_data)
{
    return request(
        peer, &(struct request){ .code = STOW_MSG_SET, .ids = ids, .values = values, .count = count }, cb, user_data,
        NULL);
}

int stow_client_subscribe_async(
    struct stow_client_peer* peer, const uint16_t* ids, size_t count, stow_client_cb_t cb, void* user_data)
{
    return request(
        peer, &(struct request){ .code = STOW_MSG_SUBSCRIBE, .ids = ids, .count = count }, cb, user_data, NULL);
}

int stow_client_unsubscribe_async(
    struct stow_client_peer* peer, const uint16_t* ids, size_t count, stow_client_cb_t cb, void* user_data)
{
    return request(
        peer, &(struct request){ .code = STOW_MSG_UNSUBSCRIBE, .ids = ids, .count = count }, cb, user_data, NULL);
}

void stow_client_release(const struct stow_client_endpoint* endpoint, struct stow_client_value* value)
{
    if (value->id < endpoint->schema->item_count)
    {
        data_value_t* item_value = &value->value;
        stow_item_release(endpoint->schema->items[value->id].interface, item_value);
    }
}

void stow_client_ref(const struct stow_client_endpoint* endpoint, struct stow_client_value* value)
{
    if (value->id < endpoint->schema->item_count)
    {
        data_value_t* item_value = &value->value;
        stow_item_ref(endpoint->schema->items[value->id].interface, item_value);
        PASS_OWNERSHIP(item_value);
    }
}
