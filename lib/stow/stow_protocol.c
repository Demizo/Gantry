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
#include <gantry/stow/stow_describe.h>
#include <gantry/stow/stow_event.h>
#include <gantry/stow/stow_protocol.h>
#include <gantry/stow/stow_serde.h>
#include <generated_stow_items.h>
#include <stddef.h>
#include <string.h>
#include <sys/errno.h>
#include <zcbor_decode.h>
#include <zcbor_encode.h>
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

/**
 * @brief Number of zcbor encoder/decoder state slots
 *
 * @details Two is sufficient for the flat request/response framing; the
 * value-encoding helpers nest at most one level deeper.
 */
#define ZCBOR_STATES 4

/**
 * @brief Bytes a Describe Response needs before its chunk, at most
 *
 * @details The list header, message code, tag, next item ID and the chunk's byte string header.
 */
#define DESCRIBE_RESPONSE_OVERHEAD 13

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
 * @brief A response being built for one request
 */
struct response
{
    struct net_buf* buf;                 /**< Buffer the response is encoded into */
    uint8_t* start;                      /**< Start of the encoded response */
    size_t budget;                       /**< Bytes the encoder may use */
    uint8_t tag;                         /**< Tag of the request being answered */
    zcbor_state_t encoder[ZCBOR_STATES]; /**< Encoder positioned after the encoded elements */
};

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

static int response_open(struct protocol_session* session, uint8_t tag, struct response* response);
static enum stow_error_code error_code_from_errno(int err);
static void response_fail(struct response* response, int err, uint32_t item, uint32_t index);
static void response_check(struct response* response, int ret, uint32_t item);
static void response_send(struct protocol_session* session, struct response* response);
static void response_ok(struct response* response);
static void emit_update(struct protocol_session* session, enum stow_item_id id, data_value_t value);
static void remove_subscription(struct protocol_session* session, enum stow_item_id id);

static void dispatch_request(
    struct protocol_session* session, const struct stow_serde_request* request, struct response* response);
static int encode_hello(struct protocol_session* session, struct response* response);
static void handle_hello(struct protocol_session* session, struct response* response);
static int encode_describe(struct response* response, uint32_t start_id, const atomic_t* filter, uint32_t* next_id_out);
#ifdef CONFIG_STOW_PROTOCOL_RESPONSE_CHECK
static void check_responses_fit(struct protocol_session* session);
#endif
static void handle_describe(const struct stow_serde_request* request, struct response* response);
static void handle_get(
    struct protocol_session* session, const struct stow_serde_request* request, struct response* response);
static void handle_set(
    struct protocol_session* session, const struct stow_serde_request* request, struct response* response);
static int add_subscription(struct protocol_session* session, enum stow_item_id id);
static void handle_subscribe(
    struct protocol_session* session, const struct stow_serde_request* request, struct response* response);
static void handle_unsubscribe(
    struct protocol_session* session, const struct stow_serde_request* request, struct response* response);

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
 */
static int send_message(struct protocol_session* session, struct net_buf* buf)
{
    int ret = session->transport->send(session->transport, session->session_id, buf);
    if (ret != SUCCESS)
    {
        net_buf_unref(buf);
    }
    return ret;
}

/**
 * @brief Start a message to a session
 *
 * @param session Session the message is for
 * @param tag Tag of the request being answered, or @ref STOW_TAG_NONE for an Update
 * @param[out] response Populated with a buffer and an encoder positioned at its start
 *
 * @return SUCCESS when the message is ready to encode
 * @return -ENOMEM when no buffer is available
 */
static int response_open(struct protocol_session* session, uint8_t tag, struct response* response)
{
    const struct stow_transport* transport = session->transport;
    struct net_buf* buf = net_buf_alloc(transport->pool, K_NO_WAIT);
    if (buf == NULL)
    {
        return -ENOMEM;
    }
    net_buf_reserve(buf, transport->headroom);

    size_t tail = net_buf_tailroom(buf);
    ASSERT(tail >= transport->tailroom, "Provided buffers are too small");

    response->buf = buf;
    response->budget = tail - transport->tailroom;
    response->tag = tag;
    response->start = net_buf_tail(buf);
    zcbor_new_encode_state(response->encoder, ARRAY_SIZE(response->encoder), response->start, response->budget, 1);
    PASS_OWNERSHIP(buf);
    return SUCCESS;
}

/**
 * @brief Map an errno to a protocol error code
 *
 * @param err Negative errno
 *
 * @return The matching protocol error code
 */
static enum stow_error_code error_code_from_errno(int err)
{
    switch (err)
    {
        case -EBADMSG:
            return STOW_ERR_MALFORMED_MSG;
        case -ENOTSUP:
            return STOW_ERR_UNKNOWN_MSG;
        case -ENOENT:
            return STOW_ERR_INVALID_ITEM;
        case -EINVAL:
            return STOW_ERR_INVALID_VALUE;
        case -ENOMEM:
            return STOW_ERR_OUT_OF_MEMORY;
        case -EACCES:
            return STOW_ERR_PERMISSION_DENIED;
        case -EALREADY:
            return STOW_ERR_ALREADY_SET;
        case -E2BIG:
            return STOW_ERR_TOO_LARGE;
        default:
            return STOW_ERR_UNKNOWN;
    }
}

/**
 * @brief Replace whatever was encoded in a response with an Error
 *
 * @details An Error that does not fit the response buffer is logged and the response is dropped.
 *
 * @param response The response
 * @param err Negative errno describing the failure
 * @param item Item that failed, or @ref STOW_SERDE_ABSENT
 * @param index Entry that failed as it was applied, or @ref STOW_SERDE_ABSENT
 */
static void response_fail(struct response* response, int err, uint32_t item, uint32_t index)
{
    struct stow_serde_error error = {
        .code = error_code_from_errno(err),
        .tag = response->tag,
        .item = item,
        .index = index,
    };

    zcbor_new_encode_state(response->encoder, ARRAY_SIZE(response->encoder), response->start, response->budget, 1);
    if (stow_serde_encode_error(response->encoder, &error) != SUCCESS)
    {
        LOG_ERR("Transport buffers cannot hold an Error, dropping the response");
        struct net_buf* buf = response->buf;
        net_buf_unref(buf);
        response->buf = NULL;
    }
}

/**
 * @brief Turn a failed encode into a TOO_LARGE error
 *
 * @param response The response
 * @param ret Result of encoding the response
 * @param item Item being encoded, or @ref STOW_SERDE_ABSENT
 */
static void response_check(struct response* response, int ret, uint32_t item)
{
    if (ret != SUCCESS)
    {
        response_fail(response, (ret == -ENOMEM) ? -E2BIG : ret, item, STOW_SERDE_ABSENT);
    }
}

/**
 * @brief Hand a finished response to the session's transport
 *
 * @param session Session the response belongs to
 * @param response The response, its buffer is always consumed
 */
static void response_send(struct protocol_session* session, struct response* response)
{
    if (response->buf == NULL)
    {
        return;
    }

    net_buf_add(response->buf, response->encoder[0].payload - response->start);
    int ret = send_message(session, response->buf);
    response->buf = NULL;
    if (ret != SUCCESS)
    {
        LOG_WRN("Dropped a message to session %u (%d)", session->session_id, ret);
    }
}

/**
 * @brief Encode an OK response
 *
 * @param response The response
 */
static void response_ok(struct response* response)
{
    response_check(response, stow_serde_encode_ok(response->encoder, response->tag), STOW_SERDE_ABSENT);
}

static void emit_update(struct protocol_session* session, enum stow_item_id id, data_value_t value)
{
    struct response response;
    int ret = response_open(session, STOW_TAG_NONE, &response);
    if (ret == SUCCESS)
    {
        ret = stow_serde_encode_update(response.encoder, id, value);
        if (ret != SUCCESS)
        {
            struct net_buf* buf = response.buf;
            net_buf_unref(buf);
        }
    }
    if (ret != SUCCESS)
    {
        LOG_WRN("Dropped Update for item %u to session %u (%d)", (uint32_t)id, session->session_id, ret);
        return;
    }

    response_send(session, &response);
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
 * @brief Encode a Hello Response for a session
 *
 * @param session Session the response is for
 * @param response Empty response to encode into
 *
 * @return SUCCESS when the response was encoded
 * @return -ENOMEM when it does not fit
 * @return the error from @ref STOW_GET when the StowHash cannot be read
 */
static int encode_hello(struct protocol_session* session, struct response* response)
{
    data_value_t hash = { 0 };
    int ret = STOW_GET(STOW_ID_STOW_HASH, &hash);
    if (ret != SUCCESS)
    {
        return ret;
    }

    struct stow_serde_hello hello = {
        .max_message = (uint32_t)response->budget,
        .stow_hash = hash.data.string_value,
        .roles = session->roles,
    };
    ret = stow_serde_encode_hello_response(response->encoder, response->tag, &hello);
    STOW_RELEASE(STOW_ID_STOW_HASH, &hash);
    return ret;
}

static void handle_hello(struct protocol_session* session, struct response* response)
{
    response_check(response, encode_hello(session, response), STOW_SERDE_ABSENT);
}

/**
 * @brief Encode a Describe Response whose chunk fills the response's budget with whole items
 *
 * @details The chunk is encoded in place after room for the response's prefix, which is then encoded in front of it.
 *
 * @param response Empty response to encode into
 * @param start_id First item ID to describe
 * @param filter Bitmap of the item IDs to describe, or NULL for every item
 * @param[out] next_id_out Populated with the next item ID to describe, or the item that does not fit
 *
 * @return SUCCESS when the response was encoded
 * @return -E2BIG when the first item to describe cannot be encoded into an otherwise empty response
 */
static int encode_describe(struct response* response, uint32_t start_id, const atomic_t* filter, uint32_t* next_id_out)
{
    *next_id_out = start_id;
    if (response->budget <= DESCRIBE_RESPONSE_OVERHEAD)
    {
        return -E2BIG;
    }

    uint8_t* chunk = response->start + DESCRIBE_RESPONSE_OVERHEAD;
    zcbor_state_t inner[ZCBOR_STATES];
    zcbor_new_encode_state(inner, ARRAY_SIZE(inner), chunk, response->budget - DESCRIBE_RESPONSE_OVERHEAD, 1);

    int ret = stow_describe(start_id, filter, inner, next_id_out);
    if (ret == -E2BIG)
    {
        return ret;
    }

    size_t chunk_len = inner[0].payload - chunk;
    uint32_t next_id = (*next_id_out < STOW_ID_COUNT) ? *next_id_out : STOW_SERDE_ABSENT;
    return stow_serde_encode_describe_response(response->encoder, response->tag, next_id, chunk, chunk_len);
}

#ifdef CONFIG_STOW_PROTOCOL_RESPONSE_CHECK
/**
 * @brief Assert that every item's description and the Hello Response fit a session's transport
 *
 * @details Encodes into scratch buffers from the transport's pool, and skips the check when none is free.
 *
 * @param session Session whose transport is checked
 */
static void check_responses_fit(struct protocol_session* session)
{
    if (!atomic_test_and_clear_bit(session->signals, SESSION_SIGNAL_CHECK))
    {
        return;
    }

    struct response response;
    if (response_open(session, STOW_TAG_MAX, &response) != SUCCESS)
    {
        LOG_WRN("Skipped the response check for session %u, no buffer free", session->session_id);
        return;
    }
    int ret = encode_hello(session, &response);
    struct net_buf* buf = response.buf;
    net_buf_unref(buf);
    ASSERT(ret == SUCCESS, "The Hello Response does not fit this transport (%d)", ret);

    for (uint32_t id = 0; id < STOW_ID_COUNT; id++)
    {
        if (response_open(session, STOW_TAG_MAX, &response) != SUCCESS)
        {
            LOG_WRN("Skipped the response check for session %u, no buffer free", session->session_id);
            return;
        }

        ATOMIC_DEFINE(filter, STOW_ID_COUNT) = { 0 };
        atomic_set_bit(filter, (int)id);
        uint32_t next_id = 0;
        ret = encode_describe(&response, id, filter, &next_id);
        buf = response.buf;
        net_buf_unref(buf);
        ASSERT(
            ret == SUCCESS, "Item %u's description does not fit a Describe Response on this transport (%d)", id, ret);
    }
}
#endif

static void handle_describe(const struct stow_serde_request* request, struct response* response)
{
    uint32_t next_id = 0;
    const atomic_t* filter = request->has_filter ? request->filter : NULL;
    int ret = encode_describe(response, (uint32_t)request->item_id, filter, &next_id);
    if (ret == -E2BIG)
    {
        response_fail(response, ret, next_id, STOW_SERDE_ABSENT);
        return;
    }
    response_check(response, ret, STOW_SERDE_ABSENT);
}

static void handle_get(
    struct protocol_session* session, const struct stow_serde_request* request, struct response* response)
{
    zcbor_state_t entries[ZCBOR_STATES];
    stow_serde_entries_start(request, entries, ARRAY_SIZE(entries));

    int ret = stow_serde_encode_get_start(response->encoder, response->tag, request->entry_count);
    for (uint16_t i = 0; ret == SUCCESS && i < request->entry_count; i++)
    {
        uint32_t id = 0;
        ret = stow_serde_decode_entry(entries, &id);
        if (ret != SUCCESS)
        {
            response_fail(response, ret, id, i);
            return;
        }

        data_value_t value = { 0 };
        ret = STOW_GET_EXTERNAL(session->roles, (enum stow_item_id)id, &value);
        if (ret != SUCCESS)
        {
            response_fail(response, ret, id, i);
            return;
        }

        ret = stow_serde_encode_get_entry(response->encoder, (enum stow_item_id)id, value);
        STOW_RELEASE((enum stow_item_id)id, &value);
        if (ret != SUCCESS)
        {
            response_check(response, ret, id);
            return;
        }
    }
    response_check(response, ret, STOW_SERDE_ABSENT);
}

static void handle_set(
    struct protocol_session* session, const struct stow_serde_request* request, struct response* response)
{
    zcbor_state_t entries[ZCBOR_STATES];
    stow_serde_entries_start(request, entries, ARRAY_SIZE(entries));

    for (uint16_t i = 0; i < request->entry_count; i++)
    {
        uint32_t id = 0;
        int ret = stow_serde_decode_entry(entries, &id);
        if (ret != SUCCESS)
        {
            response_fail(response, ret, id, i);
            return;
        }

        data_value_t value = { 0 };
        ret = stow_decode(entries, (enum stow_item_id)id, &value);
        if (ret != SUCCESS)
        {
            response_fail(response, ret, id, i);
            return;
        }

        ret = STOW_SET_EXTERNAL(session->roles, (enum stow_item_id)id, value);
        STOW_RELEASE((enum stow_item_id)id, &value);
        if (ret != SUCCESS)
        {
            response_fail(response, ret, id, i);
            return;
        }
    }
    response_ok(response);
}

/**
 * @brief Subscribe a session to one item
 *
 * @param session Session to subscribe
 * @param id Item to subscribe to
 *
 * @return SUCCESS when the session is subscribed, including when it already was
 * @return -EACCES when the session's roles cannot read the item
 * @return the error from @ref stow_subscribe_external
 */
static int add_subscription(struct protocol_session* session, enum stow_item_id id)
{
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

static void handle_subscribe(
    struct protocol_session* session, const struct stow_serde_request* request, struct response* response)
{
    zcbor_state_t entries[ZCBOR_STATES];
    stow_serde_entries_start(request, entries, ARRAY_SIZE(entries));

    for (uint16_t i = 0; i < request->entry_count; i++)
    {
        uint32_t id = 0;
        int ret = stow_serde_decode_entry(entries, &id);
        if (ret != SUCCESS)
        {
            response_fail(response, ret, id, i);
            return;
        }

        ret = add_subscription(session, (enum stow_item_id)id);
        if (ret != SUCCESS)
        {
            response_fail(response, ret, id, i);
            return;
        }
    }
    response_ok(response);
}

static void handle_unsubscribe(
    struct protocol_session* session, const struct stow_serde_request* request, struct response* response)
{
    zcbor_state_t entries[ZCBOR_STATES];
    stow_serde_entries_start(request, entries, ARRAY_SIZE(entries));

    for (uint16_t i = 0; i < request->entry_count; i++)
    {
        uint32_t id = 0;
        int ret = stow_serde_decode_entry(entries, &id);
        if (ret != SUCCESS)
        {
            response_fail(response, ret, id, i);
            return;
        }
        remove_subscription(session, (enum stow_item_id)id);
    }
    response_ok(response);
}

static void dispatch_request(
    struct protocol_session* session, const struct stow_serde_request* request, struct response* response)
{
    switch (request->message_code)
    {
        case STOW_MSG_HELLO:
            handle_hello(session, response);
            break;
        case STOW_MSG_DESCRIBE:
            handle_describe(request, response);
            break;
        case STOW_MSG_GET:
            handle_get(session, request, response);
            break;
        case STOW_MSG_SET:
            handle_set(session, request, response);
            break;
        case STOW_MSG_SUBSCRIBE:
            handle_subscribe(session, request, response);
            break;
        case STOW_MSG_UNSUBSCRIBE:
            handle_unsubscribe(session, request, response);
            break;
        default:
            ASSERT(false, "Unexpected message code");
            break;
    }
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

    const uint8_t* data = payload->rx.buf->data;
    size_t len = payload->rx.buf->len;

    zcbor_state_t dec[ZCBOR_STATES];
    zcbor_new_decode_state(dec, ARRAY_SIZE(dec), data, len, 1, NULL, 0);

    struct stow_serde_request request = { 0 };
    int ret = stow_serde_decode_request(dec, &request);
    bool trailing = (ret == SUCCESS && dec[0].payload != data + len);

    struct response response = { 0 };
    int open_ret = response_open(session, request.tag, &response);
    if (open_ret != SUCCESS)
    {
        LOG_ERR("No response buffer for session %u (%d)", session->session_id, open_ret);
        return;
    }

    if (trailing)
    {
        LOG_WRN("Request for session %u has trailing bytes", session->session_id);
        response_fail(&response, -EBADMSG, STOW_SERDE_ABSENT, STOW_SERDE_ABSENT);
    }
    else if (ret == SUCCESS)
    {
        dispatch_request(session, &request, &response);
    }
    else
    {
        LOG_WRN("Request decode failed for session %u (%d)", session->session_id, ret);
        response_fail(&response, ret, request.error_item, STOW_SERDE_ABSENT);
    }

    response_send(session, &response);
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

    if (transport == NULL || transport->send == NULL || transport->pool == NULL ||
        transport->pool->alloc->cb != &net_buf_fixed_cb)
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
