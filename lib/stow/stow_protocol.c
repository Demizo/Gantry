/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Stow protocol server
 *
 * @details Answers each session's requests through the internal Stow API, and sends a subscribed session an Update
 * when its item changes. A session runtime instance carries the sessions and runs the server's thread.
 *
 * @author Demizo (demizodemazo@gmail.com)
 *
 */

#include <gantry/error.h>
#include <gantry/event.h>
#include <gantry/memory.h>
#include <gantry/stow/stow.h>
#include <gantry/stow/stow_event.h>
#include <gantry/stow/stow_msg.h>
#include <gantry/stow/stow_protocol.h>
#include <gantry/stow/stow_runtime.h>
#include <generated_stow_items.h>
#include <stddef.h>
#include <string.h>
#include <sys/errno.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net_buf.h>
#include <zephyr/sys/atomic.h>

#include "gantry/flags.h"
#include "gantry/stow/types/stow_types.h"
#include "zephyr/toolchain.h"

/**
 * @brief Logger for module
 */
LOG_MODULE_REGISTER(stow_protocol, CONFIG_STOW_PROTOCOL_LOG_LEVEL);

//**********************************************************
//* Local Definitions
//**********************************************************

//**********************************************************
//* Typedefs, Enums, and Structs
//**********************************************************

/**
 * @brief Per-session state
 */
struct protocol_session
{
    struct stow_session base;                    /**< The runtime's session */
    ATOMIC_DEFINE(subscriptions, STOW_ID_COUNT); /**< Bitfield of item subscriptions */
};

/**
 * @brief Applies one entry of a Set, Subscribe or Unsubscribe
 */
typedef int (*apply_fn)(struct protocol_session* session, const struct stow_msg_entry* entry);

//**********************************************************
//* Static Function Declarations
//**********************************************************

static uint8_t answer(struct stow_session* base, const struct net_buf* rx, struct stow_msg_writer* writer);
static void dispatch_request(
    struct protocol_session* session, struct stow_msg_reader* reader, const struct stow_msg* req,
    struct stow_msg_writer* writer);
static void handle_hello(struct protocol_session* session, const struct stow_msg* req, struct stow_msg_writer* writer);
static int write_hello(struct protocol_session* session, struct stow_msg_writer* writer, uint8_t tag);
static void handle_describe(struct stow_msg_reader* reader, const struct stow_msg* req, struct stow_msg_writer* writer);
static uint32_t next_described(const atomic_t* filter, uint32_t id);
static void handle_get(
    struct protocol_session* session, struct stow_msg_reader* reader, const struct stow_msg* req,
    struct stow_msg_writer* writer);
static int put_value(struct protocol_session* session, struct stow_msg_writer* writer, uint16_t id);
static void apply_entries(
    struct protocol_session* session, struct stow_msg_reader* reader, const struct stow_msg* req,
    struct stow_msg_writer* writer, apply_fn apply);
static int apply_set(struct protocol_session* session, const struct stow_msg_entry* entry);
static int apply_subscribe(struct protocol_session* session, const struct stow_msg_entry* entry);
static int apply_unsubscribe(struct protocol_session* session, const struct stow_msg_entry* entry);
static void reply_error(struct stow_msg_writer* writer, uint8_t tag, int err, uint32_t item, uint32_t index);
static int resolve_item(const struct stow_msg_items* items, struct stow_msg_entry* entry);

#ifdef CONFIG_STOW_PROTOCOL_RESPONSE_CHECK
static void check_responses_fit(struct stow_session* base);
#endif
static void close_session(struct stow_session* base);
static void change_roles(struct stow_session* base, uint32_t roles, struct net_buf* buf);
static void handle_update(struct stow_session* base, event_t* event);
static void emit_update(struct protocol_session* session, enum stow_item_id id, data_value_t value);
static void remove_subscription(struct protocol_session* session, enum stow_item_id id);
static void on_stow_update(event_t* event);

//**********************************************************
//* Static Variable Definitions
//**********************************************************

/**
 * @brief Module flags
 */
FLAGS_DEFINE(stow_protocol_flags, (INITIALIZED), NO_FLAG_RULES);

/**
 * @brief Internal stow subscription used by the protocol thread on behalf of
 * external sessions.
 */
static struct stow_subscription stow_protocol_subscription = {
    .mode = STOW_SUBSCRIPTION_COPY,
    .cb = on_stow_update,
};

/**
 * @brief Stow session slots
 */
static struct protocol_session sessions[CONFIG_STOW_PROTOCOL_MAX_SESSIONS];

/**
 * @brief Number of sessions currently subscribed to each item
 */
static uint16_t item_subscription_counts[STOW_ID_COUNT];

/**
 * @brief Resolver for every request: a wire item ID is the server's item of that ID
 */
static const struct stow_msg_items server_items = { .resolve = resolve_item };

/**
 * @brief What the server does with its sessions
 */
static const struct stow_runtime_role server_role = {
#ifdef CONFIG_STOW_PROTOCOL_RESPONSE_CHECK
    .opened = check_responses_fit,
#endif
    .closed = close_session,
    .answer = answer,
    .work = change_roles,
    .update = handle_update,
};

/**
 * @brief The server's session runtime instance and thread
 */
STOW_RUNTIME_DEFINE(
    stow_protocol, server_role, sessions, base, CONFIG_STOW_PROTOCOL_QUEUE_SIZE, CONFIG_STOW_PROTOCOL_THREAD_STACK_SIZE,
    CONFIG_STOW_PROTOCOL_THREAD_PRIORITY);

//**********************************************************
//* Static Function Definitions
//**********************************************************

/**
 * @brief Write the response or Error that a received message earns
 *
 * @details A message that is not a request earns an Error that carries its tag, if it has one.
 *
 * @param base Session the message arrived on
 * @param rx The message
 * @param writer Writer of the answer
 *
 * @return Tag of the answer
 */
static uint8_t answer(struct stow_session* base, const struct net_buf* rx, struct stow_msg_writer* writer)
{
    struct protocol_session* session = CONTAINER_OF(base, struct protocol_session, base);
    uint32_t code = STOW_MSG_ABSENT;
    uint8_t tag = STOW_TAG_NONE;
    (void)stow_msg_peek(rx->data, rx->len, &code, &tag);
    if (code != STOW_MSG_ABSENT && !stow_msg_is_request(code))
    {
        LOG_WRN("Message %u for session %u is not a request", code, base->id);
        reply_error(writer, tag, (tag == STOW_TAG_NONE) ? -EBADMSG : -EPROTO, STOW_MSG_ABSENT, STOW_MSG_ABSENT);
        return tag;
    }

    struct stow_msg_reader reader;
    struct stow_msg req;
    int ret = stow_msg_read(&reader, rx->data, rx->len, &server_items, &req);
    if (ret == SUCCESS)
    {
        dispatch_request(session, &reader, &req, writer);
    }
    else
    {
        LOG_WRN("Request decode failed for session %u (%d)", base->id, ret);
        reply_error(writer, req.tag, ret, req.error.item, STOW_MSG_ABSENT);
    }
    return req.tag;
}

static void dispatch_request(
    struct protocol_session* session, struct stow_msg_reader* reader, const struct stow_msg* req,
    struct stow_msg_writer* writer)
{
    switch (req->code)
    {
        case STOW_MSG_HELLO:
            handle_hello(session, req, writer);
            break;
        case STOW_MSG_DESCRIBE:
            handle_describe(reader, req, writer);
            break;
        case STOW_MSG_GET:
            handle_get(session, reader, req, writer);
            break;
        case STOW_MSG_SET:
            apply_entries(session, reader, req, writer, apply_set);
            break;
        case STOW_MSG_SUBSCRIBE:
            apply_entries(session, reader, req, writer, apply_subscribe);
            break;
        case STOW_MSG_UNSUBSCRIBE:
            apply_entries(session, reader, req, writer, apply_unsubscribe);
            break;
        default:
            ASSERT(false, "Unexpected message code %u", req->code);
            break;
    }
}

static void handle_hello(struct protocol_session* session, const struct stow_msg* req, struct stow_msg_writer* writer)
{
    int ret = write_hello(session, writer, req->tag);
    if (ret != SUCCESS)
    {
        reply_error(writer, req->tag, ret, STOW_MSG_ABSENT, STOW_MSG_ABSENT);
    }
}

/**
 * @brief Write a Hello Response for a session
 *
 * @param session Session the response is for
 * @param writer Writer of the response
 * @param tag Tag of the request being answered
 *
 * @return SUCCESS when the response was written
 * @return -E2BIG when it does not fit
 * @return the error from @ref STOW_GET when the StowHash cannot be read
 */
static int write_hello(struct protocol_session* session, struct stow_msg_writer* writer, uint8_t tag)
{
    data_value_t hash = { 0 };
    int ret = STOW_GET(STOW_ID_STOW_HASH, &hash);
    if (ret != SUCCESS)
    {
        return ret;
    }

    const char* text = hash.data.string_value;
    const struct stow_msg hello = {
        .code = STOW_MSG_HELLO_RESPONSE,
        .tag = tag,
        .hello = {
            .stow_hash = { .value = (const uint8_t*)text, .len = strlen(text) },
            .roles = session->base.roles,
            .role_names = g_stow_role_names,
            .role_count = STOW_ROLE_COUNT,
        },
    };
    ret = stow_msg_write(writer, &hello);
    STOW_RELEASE(STOW_ID_STOW_HASH, &hash);
    return ret;
}

static void handle_describe(struct stow_msg_reader* reader, const struct stow_msg* req, struct stow_msg_writer* writer)
{
    if (req->describe.start >= STOW_ID_COUNT)
    {
        reply_error(writer, req->tag, -ENOENT, req->describe.start, STOW_MSG_ABSENT);
        return;
    }

    ATOMIC_DEFINE(named, STOW_ID_COUNT) = { 0 };
    for (uint16_t i = 0; i < req->count; i++)
    {
        struct zcbor_string name;
        (void)stow_msg_next_name(reader, &name);
        for (uint32_t id = 0; id < STOW_ID_COUNT; id++)
        {
            const char* item_name = g_stow_const_metadata[id].name;
            if (strlen(item_name) == name.len && memcmp(item_name, name.value, name.len) == 0)
            {
                atomic_set_bit(named, (int)id);
                break;
            }
        }
    }

    (void)stow_msg_write(writer, &(struct stow_msg){ .code = STOW_MSG_DESCRIBE_RESPONSE, .tag = req->tag });
    const atomic_t* filter = req->describe.named ? named : NULL;
    uint32_t first = next_described(filter, req->describe.start);
    for (uint32_t id = first; id < STOW_ID_COUNT; id = next_described(filter, id + 1))
    {
        if (stow_msg_put_description(writer, &g_stow_const_metadata[id]) != SUCCESS)
        {
            if (id == first)
            {
                reply_error(writer, req->tag, -E2BIG, id, STOW_MSG_ABSENT);
            }
            return;
        }
    }
}

/**
 * @brief Find the next item to describe
 *
 * @param filter Bitmap of item IDs to describe, or NULL for every item
 * @param id First item ID to consider
 *
 * @return The first described item ID at or after @p id, or STOW_ID_COUNT when there is none
 */
static uint32_t next_described(const atomic_t* filter, uint32_t id)
{
    while (id < STOW_ID_COUNT && filter != NULL && !atomic_test_bit(filter, (int)id))
    {
        id++;
    }
    return id;
}

static void handle_get(
    struct protocol_session* session, struct stow_msg_reader* reader, const struct stow_msg* req,
    struct stow_msg_writer* writer)
{
    (void)stow_msg_write(
        writer, &(struct stow_msg){ .code = STOW_MSG_GET_RESPONSE, .tag = req->tag, .count = req->count });
    for (uint16_t i = 0; i < req->count; i++)
    {
        struct stow_msg_entry entry;
        int ret = stow_msg_next_entry(reader, &entry);
        if (ret == SUCCESS)
        {
            ret = put_value(session, writer, entry.local);
            stow_msg_entry_release(&entry);
        }
        if (ret != SUCCESS)
        {
            reply_error(writer, req->tag, ret, entry.id, (ret == -E2BIG) ? STOW_MSG_ABSENT : i);
            return;
        }
    }
}

/**
 * @brief Read an item with the session's roles and put it as the next entry of a Get Response
 *
 * @param session Session whose roles the read is checked against
 * @param writer Writer of the Get Response
 * @param id Item to read
 *
 * @return SUCCESS, the error from @ref STOW_GET_EXTERNAL, or the writer's error
 */
static int put_value(struct protocol_session* session, struct stow_msg_writer* writer, uint16_t id)
{
    data_value_t value = { 0 };
    int ret = STOW_GET_EXTERNAL(session->base.roles, (enum stow_item_id)id, &value);
    if (ret != SUCCESS)
    {
        return ret;
    }
    const struct stow_msg_entry entry = { .id = id, .codec = g_stow_const_metadata[id].interface, .value = value };
    ret = stow_msg_put_entry(writer, &entry);
    STOW_RELEASE((enum stow_item_id)id, &value);
    return ret;
}

/**
 * @brief Apply each entry in order, answering OK or an Error naming the first entry that fails
 *
 * @details Entries before the failing one stay applied; the Error carries the failing entry's index.
 *
 * @param session Session the request arrived on
 * @param reader Reader of the request
 * @param req The request
 * @param writer Writer of the answer
 * @param apply Applies one entry
 */
static void apply_entries(
    struct protocol_session* session, struct stow_msg_reader* reader, const struct stow_msg* req,
    struct stow_msg_writer* writer, apply_fn apply)
{
    for (uint16_t i = 0; i < req->count; i++)
    {
        struct stow_msg_entry entry;
        int ret = stow_msg_next_entry(reader, &entry);
        if (ret == SUCCESS)
        {
            ret = apply(session, &entry);
            stow_msg_entry_release(&entry);
        }
        if (ret != SUCCESS)
        {
            reply_error(writer, req->tag, ret, entry.id, i);
            return;
        }
    }
    (void)stow_msg_write(writer, &(struct stow_msg){ .code = STOW_MSG_OK, .tag = req->tag });
}

static int apply_set(struct protocol_session* session, const struct stow_msg_entry* entry)
{
    return STOW_SET_EXTERNAL(session->base.roles, (enum stow_item_id)entry->local, entry->value);
}

/**
 * @brief Subscribe a session to one item
 *
 * @param session Session to subscribe
 * @param entry Entry naming the item
 *
 * @return SUCCESS when the session is subscribed, including when it already was
 * @return -EACCES when the session's roles cannot read the item
 * @return the error from @ref stow_subscribe_external
 */
static int apply_subscribe(struct protocol_session* session, const struct stow_msg_entry* entry)
{
    enum stow_item_id id = (enum stow_item_id)entry->local;
    if (atomic_test_bit(session->subscriptions, (int)id))
    {
        return SUCCESS;
    }

    if ((g_stow_const_metadata[id].permissions.read_permissions & session->base.roles) == 0)
    {
        return -EACCES;
    }

    if (item_subscription_counts[id] == 0)
    {
        int ret = stow_subscribe_external(session->base.roles, id, &stow_protocol_subscription);
        if (ret != SUCCESS)
        {
            return ret;
        }
    }
    item_subscription_counts[id]++;
    atomic_set_bit(session->subscriptions, (int)id);
    return SUCCESS;
}

static int apply_unsubscribe(struct protocol_session* session, const struct stow_msg_entry* entry)
{
    remove_subscription(session, (enum stow_item_id)entry->local);
    return SUCCESS;
}

/**
 * @brief Replace whatever was written with an Error
 *
 * @param writer Writer of the response
 * @param tag Tag of the request being answered, or @ref STOW_TAG_NONE
 * @param err Negative errno describing the failure
 * @param item Item that failed, or @ref STOW_MSG_ABSENT
 * @param index Entry that failed as it was applied, or @ref STOW_MSG_ABSENT
 */
static void reply_error(struct stow_msg_writer* writer, uint8_t tag, int err, uint32_t item, uint32_t index)
{
    (void)stow_msg_write(
        writer, &(struct stow_msg){ .code = STOW_MSG_ERROR, .tag = tag, .error = { err, item, index } });
}

static int resolve_item(const struct stow_msg_items* items, struct stow_msg_entry* entry)
{
    ARG_UNUSED(items);
    if (!stow_is_id_valid(entry->id))
    {
        return -ENOENT;
    }
    entry->local = (uint16_t)entry->id;
    entry->codec = g_stow_const_metadata[entry->id].interface;
    return SUCCESS;
}

#ifdef CONFIG_STOW_PROTOCOL_RESPONSE_CHECK
/**
 * @brief Assert that the Hello Response and every item's description fit a session's transport
 *
 * @details Writes into a scratch buffer from the transport's pool, and skips the check when none is free.
 *
 * @param base Session whose transport is checked
 */
static void check_responses_fit(struct stow_session* base)
{
    struct protocol_session* session = CONTAINER_OF(base, struct protocol_session, base);
    struct net_buf* buf = NULL;
    size_t budget = 0;
    if (stow_transport_alloc(base->transport, 0U, &buf, &budget) != SUCCESS)
    {
        LOG_WRN("Skipped the response check for session %u, no buffer free", base->id);
        return;
    }

    struct stow_msg_writer writer;
    stow_msg_writer_init(&writer, net_buf_tail(buf), budget);
    int ret = write_hello(session, &writer, STOW_TAG_MAX);
    ASSERT(ret == SUCCESS, "The Hello Response does not fit this transport (%d)", ret);
    for (uint32_t id = 0; id < STOW_ID_COUNT; id++)
    {
        (void)stow_msg_write(&writer, &(struct stow_msg){ .code = STOW_MSG_DESCRIBE_RESPONSE, .tag = STOW_TAG_MAX });
        ret = stow_msg_put_description(&writer, &g_stow_const_metadata[id]);
        ASSERT(
            ret == SUCCESS, "Item %u's description does not fit a Describe Response on this transport (%d)", id, ret);
    }
    net_buf_unref(buf);
}
#endif

static void close_session(struct stow_session* base)
{
    struct protocol_session* session = CONTAINER_OF(base, struct protocol_session, base);
    for (size_t id = 0; id < STOW_ID_COUNT; id++)
    {
        remove_subscription(session, (enum stow_item_id)id);
    }
}

/**
 * @brief Give a session new roles, revoking the subscriptions they cannot read
 *
 * @param base The session
 * @param roles The new role bitmask
 * @param buf Unused
 */
static void change_roles(struct stow_session* base, uint32_t roles, struct net_buf* buf)
{
    ARG_UNUSED(buf);
    struct protocol_session* session = CONTAINER_OF(base, struct protocol_session, base);
    base->roles = (stow_role_t)roles;

    for (size_t id = 0; id < STOW_ID_COUNT; id++)
    {
        if (!atomic_test_bit(session->subscriptions, (int)id))
        {
            continue;
        }
        if ((g_stow_const_metadata[id].permissions.read_permissions & base->roles) == 0)
        {
            remove_subscription(session, (enum stow_item_id)id);
        }
    }
}

/**
 * @brief Send a session the Update in a Stow update event, if it is subscribed
 *
 * @param base The session
 * @param event The Stow update event
 */
static void handle_update(struct stow_session* base, event_t* event)
{
    struct protocol_session* session = CONTAINER_OF(base, struct protocol_session, base);
    const struct stow_update_event_payload* payload = (const struct stow_update_event_payload*)event->data.buf;
    const enum stow_item_id id = (enum stow_item_id)payload->metadata->id;

    if (!atomic_test_bit(session->subscriptions, (int)id))
    {
        return;
    }

    if ((payload->metadata->permissions.read_permissions & base->roles) == 0)
    {
        // Auto-revoke defensively in case the role handler missed it.
        remove_subscription(session, id);
        return;
    }

    emit_update(session, id, payload->value_copy);
}

static void emit_update(struct protocol_session* session, enum stow_item_id id, data_value_t value)
{
    struct net_buf* buf = NULL;
    size_t budget = 0;
    int ret = stow_transport_alloc(session->base.transport, 0U, &buf, &budget);
    if (ret == SUCCESS)
    {
        struct stow_msg_writer writer;
        stow_msg_writer_init(&writer, net_buf_tail(buf), budget);
        (void)stow_msg_write(&writer, &(struct stow_msg){ .code = STOW_MSG_UPDATE, .tag = STOW_TAG_NONE, .count = 1 });
        const struct stow_msg_entry entry = { .id = id, .codec = g_stow_const_metadata[id].interface, .value = value };
        (void)stow_msg_put_entry(&writer, &entry);

        size_t len = 0;
        ret = stow_msg_finish(&writer, &len);
        if (ret == SUCCESS)
        {
            net_buf_add(buf, len);
            ret = stow_transport_send(session->base.transport, session->base.id, buf);
        }
        else
        {
            net_buf_unref(buf);
        }
    }
    if (ret != SUCCESS)
    {
        LOG_WRN("Dropped Update for item %u to session %u (%d)", (uint32_t)id, session->base.id, ret);
    }
}

static void remove_subscription(struct protocol_session* session, enum stow_item_id id)
{
    if (!atomic_test_bit(session->subscriptions, (int)id))
    {
        // Already not subscribed
        return;
    }

    atomic_clear_bit(session->subscriptions, (int)id);
    item_subscription_counts[id]--;

    if (item_subscription_counts[id] == 0)
    {
        (void)stow_unsubscribe(id, &stow_protocol_subscription);
    }
}

static void on_stow_update(event_t* event)
{
    EVENT_REF(event);
    if (stow_runtime_broadcast(&stow_protocol, event) != SUCCESS)
    {
        LOG_WRN("Event dropped, queue full");
    }
}

//**********************************************************
//* Public Function Definitions
//**********************************************************

int stow_protocol_init(void)
{
    if (CHECK_FLAG(stow_protocol_flags, INITIALIZED))
    {
        return -EALREADY;
    }

    SET_FLAG(stow_protocol_flags, INITIALIZED);
    return stow_runtime_start(&stow_protocol);
}

int stow_protocol_session_set_roles(uint32_t session_id, stow_role_t roles)
{
    ASSERT(CHECK_FLAG(stow_protocol_flags, INITIALIZED), "Not initialized");

    /* The resource checker takes only an identifier for a released argument */
    struct net_buf* no_buffer = NULL;
    int ret = stow_runtime_post(&stow_protocol, &stow_protocol_endpoint, session_id, roles, no_buffer);
    if (ret == -ENOTCONN)
    {
        LOG_WRN("session_set_roles for unknown session %u", session_id);
        return SUCCESS;
    }
    return ret;
}

const struct stow_endpoint stow_protocol_endpoint = STOW_RUNTIME_ENDPOINT(stow_protocol);
