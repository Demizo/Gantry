/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Stow client
 *
 * @details One thread serves every peer: it verifies peers, sends every request, matches responses by tag and fails
 * requests that time out or outlive their session.
 *
 * @author Demizo (demizodemazo@gmail.com)
 *
 */

#include <gantry/error.h>
#include <gantry/memory.h>
#include <gantry/stow/stow_client.h>
#include <gantry/stow/stow_client_serde.h>
#include <gantry/stow/stow_transport.h>
#include <gantry/stow/stow_wire.h>
#include <stddef.h>
#include <string.h>
#include <sys/errno.h>
#include <zcbor_decode.h>
#include <zcbor_encode.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net_buf.h>
#include <zephyr/sys/atomic.h>

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

/**
 * @brief Kind of work queued for the client thread
 */
enum client_event_type
{
    CLIENT_EVENT_RX,   /**< A message arrived */
    CLIENT_EVENT_SEND, /**< A request is ready to send */
};

/**
 * @brief Work queued for the client thread
 */
struct client_event
{
    enum client_event_type type; /**< Kind of work */
    uint8_t peer;                /**< Index of the peer */
    uint8_t slot;                /**< Index of a request's slot */
    uint32_t seq;                /**< Sequence number the slot had when the request was queued */
    uint32_t opens;              /**< Session count of the peer when a message arrived */
    struct net_buf* buf;         /**< The message */
};

//**********************************************************
//* Static Function Declarations
//**********************************************************

static void client_thread(void* p1, void* p2, void* p3);
static void update_event_on_free(event_t* event);
static void result_event_on_free(event_t* event);
static struct stow_client_peer* find_open_peer(const struct stow_endpoint* endpoint, uint32_t session_id);
static bool push_event(const struct client_event* event);

static int64_t next_deadline(void);
static void expire(void);

static int encode_request(
    struct stow_client_peer* peer, struct stow_transport* transport, uint8_t tag, const struct request* req,
    struct net_buf** buf_out);
static int check_request(const struct stow_client_schema* schema, const struct request* req);
static int check_available(struct stow_client_peer* peer, const struct request* req);
static int request(
    struct stow_client_peer* peer, const struct request* req, stow_client_cb_t cb, void* user_data,
    struct sync_call* sync);
static int request_sync(struct stow_client_peer* peer, const struct request* req, struct stow_client_value* values_out);

static int find_local(struct stow_client_peer* peer, uint32_t remote_id);
static void release_values(struct stow_client_peer* peer, struct stow_client_value* values, size_t count);
static int decode_values(
    struct stow_client_peer* peer, zcbor_state_t* decoder, size_t count, struct stow_client_value* values);
static void complete_get(struct stow_client_peer* peer, struct slot* slot, zcbor_state_t* decoder);
static void complete_request(struct stow_client_peer* peer, struct slot* slot, uint32_t code, zcbor_state_t* decoder);
static void handle_update(struct stow_client_peer* peer, zcbor_state_t* decoder);
static void handle_rx(struct stow_client_peer* peer, struct net_buf* buf, uint32_t opens);
static void handle_send(const struct client_event* event);

static void close_peer(struct stow_client_peer* peer);
static void handle_signals(void);

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
 * @brief Guards which peer slots are in use and which sessions are open
 */
static struct k_spinlock peers_lock;

/**
 * @brief Work queued for the client thread
 */
K_MSGQ_DEFINE(stow_client_queue, sizeof(struct client_event), QUEUE_SIZE, sizeof(void*));

/**
 * @brief Wakes the client thread when work or signals are pending
 */
K_SEM_DEFINE(stow_client_wake, 0, 1);

/**
 * @brief Client thread, started at boot
 */
K_THREAD_DEFINE(
    stow_client_thread_id, CONFIG_STOW_CLIENT_THREAD_STACK_SIZE, client_thread, NULL, NULL, NULL,
    CONFIG_STOW_CLIENT_THREAD_PRIORITY, 0, 0);

//**********************************************************
//* Static Function Definitions
//**********************************************************

/**
 * @brief Find the peer whose session is open on an endpoint and not closing
 *
 * @note Call with @ref peers_lock held.
 */
static struct stow_client_peer* find_open_peer(const struct stow_endpoint* endpoint, uint32_t session_id)
{
    for (size_t i = 0; i < ARRAY_SIZE(peers); i++)
    {
        struct stow_client_peer* peer = &peers[i];
        if (peer->in_use && peer->transport != NULL && !atomic_test_bit(peer->signals, PEER_SIGNAL_CLOSE) &&
            &peer->endpoint->endpoint == endpoint && peer->session_id == session_id)
        {
            return peer;
        }
    }
    return NULL;
}

static bool push_event(const struct client_event* event)
{
    if (k_msgq_put(&stow_client_queue, event, K_NO_WAIT) != 0)
    {
        return false;
    }
    k_sem_give(&stow_client_wake);
    return true;
}

/**
 * @brief Find when the next sent request times out
 *
 * @return Uptime in ms, or INT64_MAX when nothing is sent
 */
static int64_t next_deadline(void)
{
    int64_t deadline = INT64_MAX;
    for (size_t p = 0; p < ARRAY_SIZE(peers); p++)
    {
        deadline = MIN(deadline, z_stow_client_slots_deadline(&peers[p]));
    }
    return deadline;
}

/**
 * @brief Fail every sent request whose time is up, and a verification that timed out
 */
static void expire(void)
{
    int64_t now = k_uptime_get();
    for (size_t p = 0; p < ARRAY_SIZE(peers); p++)
    {
        struct stow_client_peer* peer = &peers[p];
        if (z_stow_client_slots_expire(peer, now))
        {
            z_stow_client_verify_failed(peer, -ETIMEDOUT);
        }
    }
}

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
    zcbor_state_t encoder[ZCBOR_STATES];
    int ret = stow_wire_message_alloc(transport, peer->info.max_message, &buf, encoder, ARRAY_SIZE(encoder));
    if (ret != SUCCESS)
    {
        return ret;
    }

    uint32_t elements = (uint32_t)((req->values != NULL) ? 2U * req->count : req->count);
    ret = stow_client_serde_encode_start(encoder, req->code, tag, elements);
    for (size_t i = 0; ret == SUCCESS && i < req->count; i++)
    {
        ret = stow_client_serde_encode_id(encoder, peer->items[req->ids[i]].remote_id);
        if (ret == SUCCESS && req->values != NULL)
        {
            ret = peer_item(peer, req->ids[i])->interface->encode(encoder, req->values[i]);
        }
    }
    if (ret != SUCCESS)
    {
        net_buf_unref(buf);
        return (ret == -ENOMEM) ? -EMSGSIZE : ret;
    }

    stow_wire_message_end(buf, encoder);
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
    if (!peer->in_use || peer->state != PEER_READY)
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
    int ret = check_request(peer->endpoint->schema, req);
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
    while (slot == NULL)
    {
        int64_t retry_at = INT64_MAX;
        bool more_free = false;
        k_spinlock_key_t key = k_spin_lock(&peer->lock);
        ret = check_available(peer, req);
        if (ret == SUCCESS)
        {
            slot = z_stow_client_slot_take(peer, &pending, &retry_at);
            transport = peer->transport;
            seq = (slot != NULL) ? slot->seq : 0U;
            more_free = z_stow_client_slot_any_free(peer);
        }
        k_spin_unlock(&peer->lock, key);

        if (ret != SUCCESS || (slot == NULL && sync == NULL))
        {
            MEM_UNREF(&ids_copy);
            EVENT_UNREF(&result);
            if (waited)
            {
                k_sem_give(&peer->slot_free);
            }
            return (ret != SUCCESS) ? ret : -EAGAIN;
        }
        if (slot == NULL)
        {
            k_timeout_t wait = (retry_at == INT64_MAX) ? K_FOREVER : K_MSEC(MAX(retry_at - k_uptime_get(), 0));
            (void)k_sem_take(&peer->slot_free, wait);
            waited = true;
        }
        else if (more_free)
        {
            k_sem_give(&peer->slot_free);
        }
    }
    PASS_OWNERSHIP(ids_copy);
    PASS_OWNERSHIP(result);

    struct net_buf* buf = NULL;
    ret = encode_request(peer, transport, slot->tag, req, &buf);
    if (ret != SUCCESS)
    {
        return z_stow_client_slot_cancel(peer, slot, seq) ? ret : SUCCESS;
    }

    struct client_event event = {
        .type = CLIENT_EVENT_SEND,
        .peer = (uint8_t)(peer - peers),
        .slot = (uint8_t)(slot - peer->slots.entries),
        .seq = seq,
        .buf = buf,
    };
    if (!push_event(&event))
    {
        net_buf_unref(buf);
        return z_stow_client_slot_cancel(peer, slot, seq) ? -EBUSY : SUCCESS;
    }
    PASS_OWNERSHIP(buf);
    return SUCCESS;
}

/**
 * @brief Send a request and block until it finishes
 *
 * @param values_out Where a Get's values go, otherwise NULL
 */
static int request_sync(struct stow_client_peer* peer, const struct request* req, struct stow_client_value* values_out)
{
    if (k_is_in_isr() || k_current_get() == stow_client_thread_id)
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
 * @brief Find the available item a peer's item ID belongs to
 *
 * @return The generated item ID, or -ENOENT
 */
static int find_local(struct stow_client_peer* peer, uint32_t remote_id)
{
    for (uint16_t id = 0; id < peer_item_count(peer); id++)
    {
        if (peer->items[id].availability == STOW_CLIENT_AVAILABLE && peer->items[id].remote_id == remote_id)
        {
            return id;
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
        stow_client_release(peer->endpoint, value);
    }
}

/**
 * @brief Decode the id/value pairs of a Get Response or an Update
 *
 * @return SUCCESS, -ENOMSG for a malformed pair or an item that is not available, or -ENOBUFS
 * @acquires memory values on-success
 */
static int decode_values(
    struct stow_client_peer* peer, zcbor_state_t* decoder, size_t count, struct stow_client_value* values)
{
    for (size_t i = 0; i < count; i++)
    {
        uint32_t remote_id = 0;
        int id = zcbor_uint32_decode(decoder, &remote_id) ? find_local(peer, remote_id) : -ENOENT;
        int ret = (id >= 0) ? peer_item(peer, (uint16_t)id)->interface->decode(decoder, &values[i].value) : -ENOENT;
        if (ret != SUCCESS)
        {
            release_values(peer, values, i);
            return (ret == -ENOMEM) ? -ENOBUFS : -ENOMSG;
        }
        values[i].id = (uint16_t)id;
    }
    PASS_OWNERSHIP(values);
    return SUCCESS;
}

/**
 * @brief Finish a Get with the values in its response, which must name the requested items in order
 */
static void complete_get(struct stow_client_peer* peer, struct slot* slot, zcbor_state_t* decoder)
{
    size_t count = slot->count;
    struct stow_client_result_payload* result =
        (slot->event != NULL) ? (struct stow_client_result_payload*)slot->event->data.buf : NULL;
    struct stow_client_value* values = (result != NULL) ? result->values : slot->sync->values;
    int ret = decode_values(peer, decoder, count, values);
    if (ret != SUCCESS)
    {
        z_stow_client_slot_fail(peer, slot, ret);
        return;
    }

    bool matches = zcbor_list_end_decode(decoder);
    for (size_t i = 0; matches && i < count; i++)
    {
        matches = (values[i].id == slot->ids[i]);
    }
    if (!matches)
    {
        release_values(peer, values, count);
        z_stow_client_slot_fail(peer, slot, -ENOMSG);
        return;
    }

    if (result != NULL)
    {
        result->count = (uint16_t)count;
    }
    PASS_OWNERSHIP(values);
    z_stow_client_slot_finish(peer, slot, SUCCESS, STOW_CLIENT_NO_INDEX);
}

/**
 * @brief Finish an application request with its response
 */
static void complete_request(struct stow_client_peer* peer, struct slot* slot, uint32_t code, zcbor_state_t* decoder)
{
    int err = SUCCESS;
    uint16_t index = STOW_CLIENT_NO_INDEX;

    if (code == STOW_MSG_ERROR)
    {
        uint32_t entry = STOW_SERDE_ABSENT;
        err = stow_client_serde_decode_error(decoder, &entry);
        if (entry < slot->count)
        {
            index = (uint16_t)entry;
        }
    }
    else if (slot->code == STOW_MSG_GET && code == STOW_MSG_GET_RESPONSE)
    {
        complete_get(peer, slot, decoder);
        return;
    }
    else if (slot->code == STOW_MSG_GET || code != STOW_MSG_OK || !zcbor_list_end_decode(decoder))
    {
        err = -ENOMSG;
    }

    z_stow_client_slot_finish(peer, slot, err, index);
}

static void handle_update(struct stow_client_peer* peer, zcbor_state_t* decoder)
{
    if (peer->state != PEER_READY)
    {
        return;
    }

    struct stow_client_value update = { 0 };
    struct stow_client_value* values = &update;
    int ret = decode_values(peer, decoder, 1U, values);
    if (ret != SUCCESS)
    {
        LOG_WRN("Dropped an Update from session %u (%d)", peer->session_id, ret);
        return;
    }
    if (!zcbor_list_end_decode(decoder))
    {
        LOG_WRN("Dropped a malformed Update from session %u", peer->session_id);
        release_values(peer, values, 1U);
        return;
    }

    event_t* event = NULL;
    if (EVENT_ALLOC(&stow_client_update_event, sizeof(struct stow_client_update_payload), &event) != SUCCESS)
    {
        LOG_WRN("Dropped an Update from session %u, no event was free", peer->session_id);
        release_values(peer, values, 1U);
        return;
    }
    struct stow_client_peer* held = stow_client_peer_ref(peer);
    *(struct stow_client_update_payload*)event->data.buf = (struct stow_client_update_payload){
        .peer = held,
        .value = update,
    };
    PASS_OWNERSHIP(held);
    PASS_OWNERSHIP(values);

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
    stow_client_release(peer->endpoint, &value);
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
        stow_client_release(peer->endpoint, &value);
    }
    stow_client_peer_unref(&peer);
}

/**
 * @brief Handle a message, dropping it when it arrived for an earlier session in the peer's slot
 */
static void handle_rx(struct stow_client_peer* peer, struct net_buf* buf, uint32_t opens)
{
    k_spinlock_key_t key = k_spin_lock(&peers_lock);
    bool current = (opens == peer->opens);
    k_spin_unlock(&peers_lock, key);

    if (!current || (peer->state != PEER_VERIFYING && peer->state != PEER_READY))
    {
        LOG_WRN("Dropping a message for session %u that is not open", peer->session_id);
        return;
    }

    zcbor_state_t decoder[ZCBOR_STATES];
    zcbor_new_decode_state(decoder, ARRAY_SIZE(decoder), buf->data, buf->len, 1, NULL, 0);

    uint32_t code = 0;
    uint8_t tag = STOW_TAG_NONE;
    if (stow_wire_decode_start(decoder, &code, &tag) != SUCCESS)
    {
        LOG_WRN("Malformed message from session %u", peer->session_id);
        return;
    }

    if (code == STOW_MSG_UPDATE)
    {
        handle_update(peer, decoder);
        return;
    }
    if (tag == STOW_TAG_NONE)
    {
        LOG_WRN("Message %u without a tag from session %u", code, peer->session_id);
        return;
    }

    struct slot* slot = z_stow_client_slot_find_sent(peer, tag);
    if (slot == NULL)
    {
        bool late = z_stow_client_tag_answered(peer, tag);
        LOG_WRN(
            "Dropped %s response with tag %u from session %u", late ? "a late" : "an unexpected", tag,
            peer->session_id);
        return;
    }

    if (!slot->internal)
    {
        complete_request(peer, slot, code, decoder);
        return;
    }

    enum stow_message_code request_code = slot->code;
    z_stow_client_slot_finish(peer, slot, SUCCESS, STOW_CLIENT_NO_INDEX);
    z_stow_client_verify_response(peer, request_code, code, decoder);
}

static void handle_send(const struct client_event* event)
{
    struct stow_client_peer* peer = &peers[event->peer];
    struct slot* slot = &peer->slots.entries[event->slot];
    struct net_buf* buf = event->buf;

    k_spinlock_key_t key = k_spin_lock(&peer->lock);
    bool current = (slot->state == SLOT_QUEUED && slot->seq == event->seq && peer->transport != NULL);
    k_spin_unlock(&peer->lock, key);
    if (!current)
    {
        net_buf_unref(buf);
        return;
    }

    (void)z_stow_client_slot_send(peer, slot, buf);
}

static void close_peer(struct stow_client_peer* peer)
{
    enum peer_state previous = peer->state;

    k_spinlock_key_t key = k_spin_lock(&peer->lock);
    peer->state = PEER_CLOSED;
    k_spin_unlock(&peer->lock, key);

    z_stow_client_slot_fail_all(peer);

    k_spinlock_key_t peers_key = k_spin_lock(&peers_lock);
    key = k_spin_lock(&peer->lock);
    peer->slots.quarantined = 0U;
    peer->transport = NULL;
    atomic_clear_bit(peer->signals, PEER_SIGNAL_CLOSE);
    k_spin_unlock(&peer->lock, key);
    k_spin_unlock(&peers_lock, peers_key);
    k_sem_give(&peer->slot_free);

    if (previous == PEER_VERIFYING)
    {
        CALL_HOOKS(connected, peer, -ENOTCONN);
    }
    else if (previous == PEER_READY)
    {
        CALL_HOOKS(disconnected, peer);
    }

    struct stow_client_peer* session = peer;
    stow_client_peer_unref(&session);
}

/**
 * @brief Act on the signals raised on every peer, closes before opens
 */
static void handle_signals(void)
{
    for (size_t i = 0; i < ARRAY_SIZE(peers); i++)
    {
        struct stow_client_peer* peer = &peers[i];
        if (!peer->in_use)
        {
            continue;
        }
        if (atomic_test_bit(peer->signals, PEER_SIGNAL_CLOSE))
        {
            atomic_clear_bit(peer->signals, PEER_SIGNAL_OPEN);
            close_peer(peer);
        }
        if (atomic_test_and_clear_bit(peer->signals, PEER_SIGNAL_OPEN))
        {
            z_stow_client_verify_start(peer);
        }
    }
}

static void client_thread(void* p1, void* p2, void* p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    for (size_t i = 0; i < ARRAY_SIZE(peers); i++)
    {
        k_sem_init(&peers[i].slot_free, 0, 1);
    }

    while (true)
    {
        int64_t deadline = next_deadline();
        k_timeout_t wait = K_FOREVER;
        if (deadline != INT64_MAX)
        {
            int64_t remaining = deadline - k_uptime_get();
            wait = K_MSEC(MAX(remaining, 0));
        }
        (void)k_sem_take(&stow_client_wake, wait);

        handle_signals();

        struct client_event event;
        while (k_msgq_get(&stow_client_queue, &event, K_NO_WAIT) == 0)
        {
            struct net_buf* buf = event.buf;
            if (event.type == CLIENT_EVENT_SEND)
            {
                handle_send(&event);
                PASS_OWNERSHIP(buf);
                continue;
            }
            handle_rx(&peers[event.peer], buf, event.opens);
            net_buf_unref(buf);
        }

        expire();
    }
}

//**********************************************************
//* Public Function Definitions
//**********************************************************

int z_stow_client_session_opened(
    const struct stow_endpoint* endpoint, uint32_t session_id, stow_role_t roles, struct stow_transport* transport)
{
    ARG_UNUSED(roles);
    const struct stow_client_endpoint* client = CONTAINER_OF(endpoint, struct stow_client_endpoint, endpoint);
    if (transport == NULL || transport->send == NULL || transport->pool == NULL ||
        transport->pool->alloc->cb != &net_buf_fixed_cb || client->schema == NULL || client->schema->item_count == 0U)
    {
        return -EINVAL;
    }

    void* items = NULL;
    if (MEM_ALLOC(client->schema->item_count * sizeof(struct item_state), &items) != SUCCESS)
    {
        return -ENOMEM;
    }

    int ret = SUCCESS;
    struct stow_client_peer* peer = NULL;
    k_spinlock_key_t key = k_spin_lock(&peers_lock);
    if (find_open_peer(endpoint, session_id) != NULL)
    {
        ret = -EALREADY;
    }
    for (size_t i = 0; ret == SUCCESS && peer == NULL && i < ARRAY_SIZE(peers); i++)
    {
        peer = peers[i].in_use ? NULL : &peers[i];
    }
    if (ret == SUCCESS && peer == NULL)
    {
        ret = -ENOMEM;
    }
    if (ret == SUCCESS)
    {
        peer->in_use = true;
        peer->opens++;
        atomic_set(&peer->refs, 1);
        peer->endpoint = client;
        peer->session_id = session_id;
        peer->timeout_ms = (client->timeout_ms != 0U) ? client->timeout_ms : CONFIG_STOW_CLIENT_TIMEOUT_MS;
        peer->items = items;
        peer->transport = transport;
        atomic_set_bit(peer->signals, PEER_SIGNAL_OPEN);
    }
    k_spin_unlock(&peers_lock, key);

    if (ret != SUCCESS)
    {
        MEM_UNREF(&items);
        return ret;
    }
    PASS_OWNERSHIP(items);
    k_sem_give(&stow_client_wake);
    return SUCCESS;
}

void z_stow_client_session_closed(const struct stow_endpoint* endpoint, uint32_t session_id)
{
    k_spinlock_key_t key = k_spin_lock(&peers_lock);
    struct stow_client_peer* peer = find_open_peer(endpoint, session_id);
    bool open = (peer != NULL && !atomic_test_and_set_bit(peer->signals, PEER_SIGNAL_CLOSE));
    k_spin_unlock(&peers_lock, key);

    if (!open)
    {
        LOG_WRN("Close for session %u that is not open", session_id);
        return;
    }
    k_sem_give(&stow_client_wake);
}

void z_stow_client_receive(const struct stow_endpoint* endpoint, uint32_t session_id, struct net_buf* buf)
{
    k_spinlock_key_t key = k_spin_lock(&peers_lock);
    struct stow_client_peer* peer = find_open_peer(endpoint, session_id);
    uint32_t opens = (peer != NULL) ? peer->opens : 0U;
    k_spin_unlock(&peers_lock, key);

    if (buf == NULL || peer == NULL)
    {
        LOG_WRN("Dropping a message for session %u that is not open", session_id);
        return;
    }

    struct net_buf* rx_buf = net_buf_ref(buf);
    struct client_event event = {
        .type = CLIENT_EVENT_RX,
        .peer = (uint8_t)(peer - peers),
        .opens = opens,
        .buf = rx_buf,
    };
    if (!push_event(&event))
    {
        LOG_WRN("Dropping a message for session %u, queue full", session_id);
        net_buf_unref(rx_buf);
        return;
    }
    PASS_OWNERSHIP(rx_buf);
}

struct stow_client_peer* stow_client_peer_ref(struct stow_client_peer* peer)
{
    atomic_val_t previous = atomic_inc(&peer->refs);
    ASSERT(previous > 0, "Peer referenced after its last reference was released");
    ARG_UNUSED(previous);
    return peer;
}

void stow_client_peer_unref(struct stow_client_peer** peer)
{
    struct stow_client_peer* released = *peer;
    *peer = NULL;
    if (released == NULL || atomic_dec(&released->refs) != 1)
    {
        return;
    }

    k_spinlock_key_t key = k_spin_lock(&peers_lock);
    void* items = released->items;
    released->items = NULL;
    released->in_use = false;
    k_spin_unlock(&peers_lock, key);
    MEM_UNREF(&items);
}

const struct stow_client_endpoint* stow_client_peer_endpoint(struct stow_client_peer* peer) { return peer->endpoint; }

uint32_t stow_client_peer_session_id(struct stow_client_peer* peer) { return peer->session_id; }

int stow_client_peer_info(struct stow_client_peer* peer, struct stow_client_peer_info* info_out)
{
    k_spinlock_key_t key = k_spin_lock(&peer->lock);
    bool ready = (peer->in_use && peer->state == PEER_READY);
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
    if (peer->in_use && peer->state == PEER_READY && id < peer_item_count(peer))
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
        endpoint->schema->items[value->id].interface->release(&value->value);
    }
}

void stow_client_ref(const struct stow_client_endpoint* endpoint, struct stow_client_value* value)
{
    if (value->id < endpoint->schema->item_count)
    {
        endpoint->schema->items[value->id].interface->ref(&value->value);
    }
}
