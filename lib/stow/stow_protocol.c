/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Stow protocol orchestration
 *
 * @details Runs a dedicated protocol thread, manages a session state table,
 * and routes external messages to the internal Stow API.
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
 * @brief Stow protocol event declaration
 */
DECLARE_EVENT_TYPE(stow_protocol_event);

/**
 * @brief Type of stow protocol event
 */
enum stow_protocol_event_type
{
    STOW_PROTOCOL_RX,          /**< Incoming CBOR payload from a session */
    STOW_PROTOCOL_SESSION_AUTH /**< Update a session's authentication roles */
};

/**
 * @brief Payload for stow protocol events
 */
struct stow_protocol_event_payload
{
    enum stow_protocol_event_type type; /**< Protocol event type */
    union
    {
        struct
        {
            uint32_t session_id; /**< Current session ID */
            struct net_buf* buf; /**< Inner Stow CBOR payload (released by on_free) */
        } rx;
        struct
        {
            uint32_t session_id; /**< Session ID to update */
            stow_role_t roles;   /**< New role bitmask for the session */
        } session_roles;
    };
};

/**
 * @brief Cleanup for stow protocol events
 *
 * @param event Event being freed
 */
static void protocol_event_on_free(event_t* event);

/**
 * @brief Stow protocol event type
 */
DEFINE_EVENT_TYPE(EVENT_ID_STOW_PROTOCOL, stow_protocol_event, protocol_event_on_free);

/**
 * @brief Signals raised on a session, handled by the protocol thread
 */
enum session_signal
{
    SESSION_SIGNAL_CLOSE, /**< The peer disconnected */
#ifdef CONFIG_STOW_PROTOCOL_RESPONSE_CHECK
    SESSION_SIGNAL_CHECK, /**< The session's responses have not been checked against its transport */
#endif
    SESSION_SIGNAL_COUNT,
};

/**
 * @brief Per-session state
 */
struct protocol_session
{
    bool in_use;                                  /**< Slot is allocated to a session */
    uint32_t session_id;                          /**< Externally provided session ID */
    stow_role_t roles;                            /**< Current role bitmask */
    struct stow_transport* transport;             /**< Transport that carries the session's messages */
    ATOMIC_DEFINE(signals, SESSION_SIGNAL_COUNT); /**< Pending @ref session_signal bits */
    ATOMIC_DEFINE(subscriptions, STOW_ID_COUNT);  /**< Bitfield of item subscriptions */
};

/**
 * @brief Applies one entry of a Set, Subscribe or Unsubscribe
 */
typedef int (*apply_fn)(struct protocol_session* session, const struct stow_msg_entry* entry);

//**********************************************************
//* Static Function Declarations
//**********************************************************

static void on_stow_update(event_t* event);
static void protocol_thread(void* a, void* b, void* c);
static void handle_event(event_t* event);

static int push_event(event_t* event);
static int alloc_protocol_event(
    enum stow_protocol_event_type type, event_t** out_event, struct stow_protocol_event_payload** out_payload);

static struct protocol_session* get_session(uint32_t session_id);
static struct protocol_session* get_open_session(uint32_t session_id);
static int signal_session(uint32_t session_id, enum session_signal signal);
static int send_message(struct protocol_session* session, struct net_buf* buf);

static void handle_rx(const struct stow_protocol_event_payload* rx_payload);
static void handle_update(event_t* event);
static void handle_session_roles(const struct stow_protocol_event_payload* auth_payload);
static void close_session(struct protocol_session* session);
static void handle_signals(void);

static int endpoint_session_opened(
    const struct stow_endpoint* endpoint, uint32_t session_id, stow_role_t roles, struct stow_transport* transport);
static void endpoint_session_closed(const struct stow_endpoint* endpoint, uint32_t session_id);
static void endpoint_receive(const struct stow_endpoint* endpoint, uint32_t session_id, struct net_buf* buf);

static int resolve_item(const struct stow_msg_items* items, struct stow_msg_entry* entry);
static void reply_error(struct stow_msg_writer* writer, uint8_t tag, int err, uint32_t item, uint32_t index);
static void emit_update(struct protocol_session* session, enum stow_item_id id, data_value_t value);
static void remove_subscription(struct protocol_session* session, enum stow_item_id id);

static void dispatch_request(
    struct protocol_session* session, struct stow_msg_reader* reader, const struct stow_msg* req,
    struct stow_msg_writer* writer);
static uint8_t answer(struct protocol_session* session, const struct net_buf* rx, struct stow_msg_writer* writer);
static int write_hello(struct protocol_session* session, struct stow_msg_writer* writer, uint8_t tag);
static void handle_hello(struct protocol_session* session, const struct stow_msg* req, struct stow_msg_writer* writer);
static uint32_t next_described(const atomic_t* filter, uint32_t id);
#ifdef CONFIG_STOW_PROTOCOL_RESPONSE_CHECK
static void check_responses_fit(struct protocol_session* session);
#endif
static void handle_describe(struct stow_msg_reader* reader, const struct stow_msg* req, struct stow_msg_writer* writer);
static int put_value(struct protocol_session* session, struct stow_msg_writer* writer, uint16_t id);
static void handle_get(
    struct protocol_session* session, struct stow_msg_reader* reader, const struct stow_msg* req,
    struct stow_msg_writer* writer);
static int apply_set(struct protocol_session* session, const struct stow_msg_entry* entry);
static int apply_subscribe(struct protocol_session* session, const struct stow_msg_entry* entry);
static int apply_unsubscribe(struct protocol_session* session, const struct stow_msg_entry* entry);
static void apply_entries(
    struct protocol_session* session, struct stow_msg_reader* reader, const struct stow_msg* req,
    struct stow_msg_writer* writer, apply_fn apply);

//**********************************************************
//* Static Variable Definitions
//**********************************************************

/**
 * @brief Module flags
 */
FLAGS_DEFINE(stow_protocol_flags, (INITIALIZED), NO_FLAG_RULES);

/**
 * @brief Message queue for module
 */
K_MSGQ_DEFINE(stow_protocol_queue, sizeof(event_t*), CONFIG_STOW_PROTOCOL_QUEUE_SIZE, sizeof(void*));

/**
 * @brief Wakes the protocol thread when events or session signals are pending
 */
K_SEM_DEFINE(stow_protocol_wake, 0, 1);

/**
 * @brief Stow protocol thread stack
 */
K_THREAD_STACK_DEFINE(stow_protocol_stack, CONFIG_STOW_PROTOCOL_THREAD_STACK_SIZE);

/**
 * @brief Stow protocol thread
 */
static struct k_thread stow_protocol_thread;

/**
 * @brief Stow protocol thread ID
 */
static k_tid_t stow_protocol_thread_id;

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

//**********************************************************
//* Static Function Definitions
//**********************************************************

static void protocol_event_on_free(event_t* event)
{
    ASSERT(event->type->id == EVENT_ID_STOW_PROTOCOL, "Unexpected event type %u", event->type->id);
    struct stow_protocol_event_payload* payload = (struct stow_protocol_event_payload*)event->data.buf;

    if (payload->type == STOW_PROTOCOL_RX && payload->rx.buf != NULL)
    {
        struct net_buf* rx_buf = payload->rx.buf;
        net_buf_unref(rx_buf);
        payload->rx.buf = NULL;
    }
}

static void on_stow_update(event_t* event)
{
    EVENT_REF(event);

    if (k_msgq_put(&stow_protocol_queue, (void*)&event, K_NO_WAIT) != 0)
    {
        LOG_WRN("Event dropped, queue full");
        EVENT_UNREF(&event);
        return;
    }
    k_sem_give(&stow_protocol_wake);
    PASS_OWNERSHIP(event);
}

/**
 * @brief Queue an event for the protocol thread, dropping it when the queue is full
 *
 * @releases memory event
 */
static int push_event(event_t* event)
{
    if (k_msgq_put(&stow_protocol_queue, (void*)&event, K_NO_WAIT) != 0)
    {
        EVENT_UNREF(&event);
        return -ENOBUFS;
    }
    k_sem_give(&stow_protocol_wake);
    PASS_OWNERSHIP(event);
    return SUCCESS;
}

/**
 * @brief Allocate a protocol event with a zeroed payload
 *
 * @acquires memory out_event on-success
 */
static int alloc_protocol_event(
    enum stow_protocol_event_type kind, event_t** out_event, struct stow_protocol_event_payload** out_payload)
{
    event_t* event = NULL;
    int ret = EVENT_ALLOC(&stow_protocol_event, sizeof(struct stow_protocol_event_payload), &event);
    if (ret != SUCCESS)
    {
        return ret;
    }

    struct stow_protocol_event_payload* payload = (struct stow_protocol_event_payload*)event->data.buf;
    memset(payload, 0, sizeof(*payload));
    payload->type = kind;

    *out_event = event;
    *out_payload = payload;
    PASS_OWNERSHIP(event);
    return SUCCESS;
}

static struct protocol_session* get_session(uint32_t session_id)
{
    for (size_t i = 0; i < ARRAY_SIZE(sessions); i++)
    {
        if (sessions[i].in_use && sessions[i].session_id == session_id)
        {
            return &sessions[i];
        }
    }
    return NULL;
}

/**
 * @brief Find a session that is open and not closing
 *
 * @note Call with interrupts locked. Only the protocol thread frees sessions, so it may keep using the result after
 * unlocking.
 *
 * @param session_id The session to find
 *
 * @return The session, or NULL
 */
static struct protocol_session* get_open_session(uint32_t session_id)
{
    struct protocol_session* session = get_session(session_id);
    if (session == NULL || atomic_test_bit(session->signals, SESSION_SIGNAL_CLOSE))
    {
        return NULL;
    }
    return session;
}

/**
 * @brief Raise a signal on a session and wake the protocol thread
 *
 * @details Signals are flags rather than queued events, so they cannot be lost when the queue is full.
 *
 * @param session_id The session to signal
 * @param signal The signal to raise
 *
 * @return SUCCESS when the signal is raised
 * @return -ENOTCONN when the session is not open
 */
static int signal_session(uint32_t session_id, enum session_signal signal)
{
    uint32_t key = irq_lock();
    struct protocol_session* session = get_open_session(session_id);
    if (session != NULL)
    {
        atomic_set_bit(session->signals, signal);
    }
    irq_unlock(key);

    if (session == NULL)
    {
        return -ENOTCONN;
    }
    k_sem_give(&stow_protocol_wake);
    return SUCCESS;
}

/**
 * @brief Hand a message to the session's transport
 *
 * @param session Session the message belongs to
 * @param buf The message, ownership is always taken
 *
 * @return SUCCESS when the transport took the message
 * @return the transport's error when it did not, the message is dropped
 * @releases net_buf buf
 */
static int send_message(struct protocol_session* session, struct net_buf* buf)
{
    return stow_transport_send(session->transport, session->session_id, buf);
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

static void emit_update(struct protocol_session* session, enum stow_item_id id, data_value_t value)
{
    struct net_buf* buf = NULL;
    size_t budget = 0;
    int ret = stow_transport_alloc(session->transport, 0U, &buf, &budget);
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
            ret = send_message(session, buf);
        }
        else
        {
            net_buf_unref(buf);
        }
    }
    if (ret != SUCCESS)
    {
        LOG_WRN("Dropped Update for item %u to session %u (%d)", (uint32_t)id, session->session_id, ret);
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
            .roles = session->roles,
            .role_names = g_stow_role_names,
            .role_count = STOW_ROLE_COUNT,
        },
    };
    ret = stow_msg_write(writer, &hello);
    STOW_RELEASE(STOW_ID_STOW_HASH, &hash);
    return ret;
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

#ifdef CONFIG_STOW_PROTOCOL_RESPONSE_CHECK
/**
 * @brief Assert that the Hello Response and every item's description fit a session's transport
 *
 * @details Writes into a scratch buffer from the transport's pool, and skips the check when none is free.
 *
 * @param session Session whose transport is checked
 */
static void check_responses_fit(struct protocol_session* session)
{
    if (!atomic_test_and_clear_bit(session->signals, SESSION_SIGNAL_CHECK))
    {
        return;
    }

    struct net_buf* buf = NULL;
    size_t budget = 0;
    if (stow_transport_alloc(session->transport, 0U, &buf, &budget) != SUCCESS)
    {
        LOG_WRN("Skipped the response check for session %u, no buffer free", session->session_id);
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
    int ret = STOW_GET_EXTERNAL(session->roles, (enum stow_item_id)id, &value);
    if (ret != SUCCESS)
    {
        return ret;
    }
    const struct stow_msg_entry entry = { .id = id, .codec = g_stow_const_metadata[id].interface, .value = value };
    ret = stow_msg_put_entry(writer, &entry);
    STOW_RELEASE((enum stow_item_id)id, &value);
    return ret;
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

static int apply_set(struct protocol_session* session, const struct stow_msg_entry* entry)
{
    return STOW_SET_EXTERNAL(session->roles, (enum stow_item_id)entry->local, entry->value);
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

    if ((g_stow_const_metadata[id].permissions.read_permissions & session->roles) == 0)
    {
        return -EACCES;
    }

    if (item_subscription_counts[id] == 0)
    {
        int ret = stow_subscribe_external(session->roles, id, &stow_protocol_subscription);
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

/**
 * @brief Write the response or Error that a received message earns
 *
 * @details A message that is not a request earns an Error that carries its tag, if it has one.
 *
 * @param session Session the message arrived on
 * @param rx The message
 * @param writer Writer of the answer
 *
 * @return Tag of the answer
 */
static uint8_t answer(struct protocol_session* session, const struct net_buf* rx, struct stow_msg_writer* writer)
{
    uint32_t code = STOW_MSG_ABSENT;
    uint8_t tag = STOW_TAG_NONE;
    (void)stow_msg_peek(rx->data, rx->len, &code, &tag);
    if (code != STOW_MSG_ABSENT && !stow_msg_is_request(code))
    {
        LOG_WRN("Message %u for session %u is not a request", code, session->session_id);
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
        LOG_WRN("Request decode failed for session %u (%d)", session->session_id, ret);
        reply_error(writer, req.tag, ret, req.error.item, STOW_MSG_ABSENT);
    }
    return req.tag;
}

static void handle_rx(const struct stow_protocol_event_payload* payload)
{
    uint32_t key = irq_lock();
    struct protocol_session* session = get_open_session(payload->rx.session_id);
    irq_unlock(key);
    if (session == NULL)
    {
        LOG_WRN("RX for closed session %u", payload->rx.session_id);
        return;
    }

    struct net_buf* buf = NULL;
    size_t budget = 0;
    int ret = stow_transport_alloc(session->transport, 0U, &buf, &budget);
    if (ret != SUCCESS)
    {
        LOG_ERR("No response buffer for session %u (%d)", session->session_id, ret);
        return;
    }
    struct stow_msg_writer writer;
    stow_msg_writer_init(&writer, net_buf_tail(buf), budget);
    uint8_t tag = answer(session, payload->rx.buf, &writer);

    size_t len = 0;
    ret = stow_msg_finish(&writer, &len);
    if (ret != SUCCESS)
    {
        reply_error(&writer, tag, ret, STOW_MSG_ABSENT, STOW_MSG_ABSENT);
        ret = stow_msg_finish(&writer, &len);
    }
    if (ret != SUCCESS)
    {
        LOG_ERR("Transport buffers cannot hold an Error, dropping the response");
        net_buf_unref(buf);
        return;
    }

    net_buf_add(buf, len);
    ret = send_message(session, buf);
    if (ret != SUCCESS)
    {
        LOG_WRN("Dropped a message to session %u (%d)", session->session_id, ret);
    }
}

static void handle_update(event_t* event)
{
    const struct stow_update_event_payload* payload = (const struct stow_update_event_payload*)event->data.buf;
    const enum stow_item_id id = (enum stow_item_id)payload->metadata->id;

    for (size_t i = 0; i < ARRAY_SIZE(sessions); i++)
    {
        struct protocol_session* session = &sessions[i];
        if (!session->in_use || atomic_test_bit(session->signals, SESSION_SIGNAL_CLOSE))
        {
            continue;
        }

        if (!atomic_test_bit(session->subscriptions, (int)id))
        {
            continue;
        }

        if ((payload->metadata->permissions.read_permissions & session->roles) == 0)
        {
            // Auto-revoke defensively in case the role handler missed it.
            remove_subscription(session, id);
            continue;
        }

        emit_update(session, id, payload->value_copy);
    }
}

static void handle_session_roles(const struct stow_protocol_event_payload* payload)
{
    uint32_t key = irq_lock();
    struct protocol_session* session = get_session(payload->session_roles.session_id);
    irq_unlock(key);
    if (session == NULL)
    {
        LOG_WRN("session_set_roles for unknown session %u", payload->session_roles.session_id);
        return;
    }

    session->roles = payload->session_roles.roles;

    for (size_t id = 0; id < STOW_ID_COUNT; id++)
    {
        if (!atomic_test_bit(session->subscriptions, (int)id))
        {
            continue;
        }
        if ((g_stow_const_metadata[id].permissions.read_permissions & session->roles) == 0)
        {
            remove_subscription(session, (enum stow_item_id)id);
        }
    }
}

static void close_session(struct protocol_session* session)
{
    for (size_t id = 0; id < STOW_ID_COUNT; id++)
    {
        if (atomic_test_bit(session->subscriptions, (int)id))
        {
            remove_subscription(session, (enum stow_item_id)id);
        }
    }

    uint32_t key = irq_lock();
    session->in_use = false;
    session->session_id = 0;
    session->transport = NULL;
    atomic_clear_bit(session->signals, SESSION_SIGNAL_CLOSE);
    irq_unlock(key);
}

/**
 * @brief Act on the signals raised on every session
 */
static void handle_signals(void)
{
    for (size_t i = 0; i < ARRAY_SIZE(sessions); i++)
    {
        struct protocol_session* session = &sessions[i];
        if (!session->in_use)
        {
            continue;
        }

        if (atomic_test_bit(session->signals, SESSION_SIGNAL_CLOSE))
        {
            close_session(session);
            continue;
        }
#ifdef CONFIG_STOW_PROTOCOL_RESPONSE_CHECK
        check_responses_fit(session);
#endif
    }
}

static void protocol_thread(void* p1, void* p2, void* p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    while (true)
    {
        (void)k_sem_take(&stow_protocol_wake, K_FOREVER);

        event_t* event = NULL;
        while (k_msgq_get(&stow_protocol_queue, (void*)&event, K_NO_WAIT) == 0)
        {
            handle_event(event);
            EVENT_UNREF(&event);
        }

        handle_signals();
    }
}

/**
 * @brief Process one event from the protocol queue
 *
 * @param event The event
 */
static void handle_event(event_t* event)
{
    if (event->type->id == EVENT_ID_STOW_UPDATE)
    {
        handle_update(event);
    }
    else if (event->type->id == EVENT_ID_STOW_PROTOCOL)
    {
        struct stow_protocol_event_payload* p = (struct stow_protocol_event_payload*)event->data.buf;
        switch (p->type)
        {
            case STOW_PROTOCOL_RX:
                handle_rx(p);
                break;
            case STOW_PROTOCOL_SESSION_AUTH:
                handle_session_roles(p);
                break;
        }
    }
    else
    {
        LOG_WRN("Unexpected event id %u", event->type->id);
    }
}

static int endpoint_session_opened(
    const struct stow_endpoint* endpoint, uint32_t session_id, stow_role_t roles, struct stow_transport* transport)
{
    ARG_UNUSED(endpoint);
    ASSERT(CHECK_FLAG(stow_protocol_flags, INITIALIZED), "Not initialized");

    if (!stow_transport_usable(transport))
    {
        return -EINVAL;
    }

    struct protocol_session* free_slot = NULL;

    uint32_t key = irq_lock();
    for (size_t i = 0; i < ARRAY_SIZE(sessions); i++)
    {
        if (sessions[i].in_use)
        {
            if (sessions[i].session_id == session_id)
            {
                // Session ID already in use
                irq_unlock(key);
                return -EALREADY;
            }
        }
        else if (free_slot == NULL)
        {
            free_slot = &sessions[i];
        }
    }

    if (free_slot == NULL)
    {
        irq_unlock(key);
        return -ENOMEM;
    }

    free_slot->session_id = session_id;
    free_slot->roles = roles;
    free_slot->transport = transport;
    atomic_clear_bit(free_slot->signals, SESSION_SIGNAL_CLOSE);
    for (int bit = 0; bit < STOW_ID_COUNT; bit++)
    {
        atomic_clear_bit(free_slot->subscriptions, bit);
    }
    free_slot->in_use = true;

    irq_unlock(key);
#ifdef CONFIG_STOW_PROTOCOL_RESPONSE_CHECK
    (void)signal_session(session_id, SESSION_SIGNAL_CHECK);
#endif
    return SUCCESS;
}

static void endpoint_session_closed(const struct stow_endpoint* endpoint, uint32_t session_id)
{
    ARG_UNUSED(endpoint);
    ASSERT(CHECK_FLAG(stow_protocol_flags, INITIALIZED), "Not initialized");

    if (signal_session(session_id, SESSION_SIGNAL_CLOSE) != SUCCESS)
    {
        LOG_WRN("Close for session %u that is not open", session_id);
    }
}

static void endpoint_receive(const struct stow_endpoint* endpoint, uint32_t session_id, struct net_buf* buf)
{
    ARG_UNUSED(endpoint);
    ASSERT(CHECK_FLAG(stow_protocol_flags, INITIALIZED), "Not initialized");

    if (buf == NULL)
    {
        LOG_WRN("Dropping NULL message for session %u", session_id);
        return;
    }

    uint32_t key = irq_lock();
    bool open = (get_open_session(session_id) != NULL);
    irq_unlock(key);
    if (!open)
    {
        LOG_WRN("Dropping message for session %u that is not open", session_id);
        return;
    }

    event_t* event = NULL;
    struct stow_protocol_event_payload* payload = NULL;
    int ret = alloc_protocol_event(STOW_PROTOCOL_RX, &event, &payload);
    if (ret != SUCCESS)
    {
        LOG_WRN("Dropping message for session %u, no event (%d)", session_id, ret);
        return;
    }

    payload->rx.session_id = session_id;
    struct net_buf* rx_buf = net_buf_ref(buf);
    payload->rx.buf = rx_buf;
    PASS_OWNERSHIP(rx_buf);

    ret = push_event(event);
    if (ret != SUCCESS)
    {
        LOG_WRN("Dropping message for session %u, queue full", session_id);
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

    memset(sessions, 0, sizeof(sessions));
    memset(item_subscription_counts, 0, sizeof(item_subscription_counts));

    stow_protocol_thread_id = k_thread_create(
        &stow_protocol_thread, stow_protocol_stack, K_THREAD_STACK_SIZEOF(stow_protocol_stack), protocol_thread, NULL,
        NULL, NULL, CONFIG_STOW_PROTOCOL_THREAD_PRIORITY, 0, K_NO_WAIT);
    k_thread_name_set(stow_protocol_thread_id, "stow_protocol");

    return SUCCESS;
}

int stow_protocol_session_set_roles(uint32_t session_id, stow_role_t roles)
{
    ASSERT(CHECK_FLAG(stow_protocol_flags, INITIALIZED), "Not initialized");

    event_t* event = NULL;
    struct stow_protocol_event_payload* payload = NULL;
    int ret = alloc_protocol_event(STOW_PROTOCOL_SESSION_AUTH, &event, &payload);
    if (ret != SUCCESS)
    {
        return ret;
    }

    payload->session_roles.session_id = session_id;
    payload->session_roles.roles = roles;

    return push_event(event);
}

const struct stow_endpoint stow_protocol_endpoint = {
    .session_opened = endpoint_session_opened,
    .session_closed = endpoint_session_closed,
    .receive = endpoint_receive,
};
