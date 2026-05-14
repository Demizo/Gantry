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

//**********************************************************
//* Typedefs, Enums, and Structs
//**********************************************************

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
 * @brief Per-session state
 */
struct protocol_session
{
    bool in_use;                                 /**< Slot is allocated to a session */
    uint32_t session_id;                         /**< Externally provided session ID */
    stow_role_t roles;                           /**< Current role bitmask */
    ATOMIC_DEFINE(subscriptions, STOW_ID_COUNT); /**< Bitfield of item subscriptions */
};

/**
 * @brief Context for encoding errors
 */
struct encode_error_context
{
    enum stow_error_code code; /**< Error code */
};

/**
 * @brief Context for encoding describe responses
 */
struct encode_describe_context
{
    const uint8_t* chunk;  /**< CBOR describe chunk */
    size_t chunk_len;      /**<Length of the describe chunk */
    uint32_t next_item_id; /**< Next item id to describe */
    bool has_more;         /**< Whether there are more items to describe */
};

/**
 * @brief Context for encoding values
 */
struct encode_value_context
{
    enum stow_item_id id; /**< Item ID */
    data_value_t value;   /**< Item value */
};

/**
 * @brief Context for encoding a multi-get response
 */
struct encode_multi_get_context
{
    const enum stow_item_id* ids; /**< Item IDs */
    const data_value_t* values;   /**< Item values */
    uint8_t count;                /**< Number of item/value pairs */
};

//**********************************************************
//* Static Function Declarations
//**********************************************************

static void on_stow_update(event_t* event);
static void protocol_thread(void* a, void* b, void* c);

static int push_event(event_t* event);
static int alloc_protocol_event(
    enum stow_protocol_event_type type, event_t** out_event, struct stow_protocol_event_payload** out_payload);

static struct protocol_session* get_session(uint32_t session_id);

static void handle_rx(const struct stow_protocol_event_payload* rx_payload);
static void handle_update(event_t* event);
static void handle_session_roles(const struct stow_protocol_event_payload* auth_payload);
static void handle_session_close(const struct stow_protocol_event_payload* close_payload);

static void dispatch_request(
    uint32_t session_id, struct protocol_session* session, const struct stow_serde_request* request);
static void handle_version(uint32_t session_id);
static void handle_describe(uint32_t session_id, enum stow_item_id start_id);
static void handle_get(uint32_t session_id, struct protocol_session* session, enum stow_item_id id);
static void handle_set(uint32_t session_id, struct protocol_session* session, const struct stow_serde_request* request);
static void handle_subscribe(uint32_t session_id, struct protocol_session* session, enum stow_item_id id);
static void handle_unsubscribe(uint32_t session_id, struct protocol_session* session, enum stow_item_id id);
static void handle_multi_get(
    uint32_t session_id, struct protocol_session* session, const struct stow_serde_request* request);
static void handle_multi_set(
    uint32_t session_id, struct protocol_session* session, const struct stow_serde_request* request);

static int emit_response(uint32_t session_id, int (*encode_fn)(zcbor_state_t*, void*), void* ctx);
static int encode_ok(zcbor_state_t* enc, void* ctx);
static int encode_version(zcbor_state_t* enc, void* ctx);
static int encode_error(zcbor_state_t* enc, void* ctx);
static int encode_describe(zcbor_state_t* enc, void* ctx);
static int encode_get_response(zcbor_state_t* enc, void* ctx);
static int encode_update(zcbor_state_t* enc, void* ctx);
static int encode_multi_get_response(zcbor_state_t* enc, void* ctx);
static void emit_error_code(uint32_t session_id, int err);
static void emit_error(uint32_t session_id, enum stow_error_code code);
static void emit_ok(uint32_t session_id);
static void emit_version_response(uint32_t session_id);
static void emit_describe_chunk(uint32_t session_id, uint32_t start_id);
static void emit_get_response(uint32_t session_id, enum stow_item_id id, data_value_t value);
static void emit_update(uint32_t session_id, enum stow_item_id id, data_value_t value);
static void emit_multi_get_response(
    uint32_t session_id, const enum stow_item_id* ids, const data_value_t* values, uint8_t count);

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
 * @brief Stow protocol module configuration
 */
static struct stow_protocol_config g_protocol_config;

/**
 * @brief Stow session slots
 */
static struct protocol_session sessions[CONFIG_STOW_PROTOCOL_MAX_SESSIONS];

/**
 * @brief Number of sessions currently subscribed to each item
 */
static uint16_t item_subscription_counts[STOW_ID_COUNT];

/**
 * @brief Local staging buffer used to encode describe chunks
 */
static uint8_t describe_chunk_staging_buffer[CONFIG_STOW_PROTOCOL_DESCRIBE_CHUNK_SIZE];

//**********************************************************
//* Static Function Definitions
//**********************************************************

static void protocol_event_on_free(event_t* event)
{
    ASSERT(event->type->id == EVENT_ID_STOW_PROTOCOL, "Unexpected event type %u", event->type->id);
    struct stow_protocol_event_payload* payload = (struct stow_protocol_event_payload*)event->data.buf;

    if (payload->type == STOW_PROTOCOL_RX && payload->rx.buf != NULL)
    {
        net_buf_unref(payload->rx.buf);
        payload->rx.buf = NULL;
    }
}

static void on_stow_update(event_t* event)
{
    if (k_msgq_put(&stow_protocol_queue, (void*)&event, K_NO_WAIT) != 0)
    {
        LOG_WRN("Event dropped, queue full");
        EVENT_UNREF(&event);
        return;
    }
    PASS_OWNERSHIP(event);
}

static int push_event(event_t* event)
{
    if (k_msgq_put(&stow_protocol_queue, (void*)&event, K_NO_WAIT) != 0)
    {
        EVENT_UNREF(&event);
        return -ENOBUFS;
    }
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
        NOT_REFERENCED(event);
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
 * @brief Allocate a response net_buf and compute the encoder budget
 *
 * @param[out] out_buf Populated with the allocated buf on success
 * @param[out] out_budget Populated with the number of bytes the encoder
 *                        may safely consume
 *
 * @return SUCCESS on success
 * @return -ENOMEM when no buf is available
 */
static int alloc_response_buf(struct net_buf** out_buf, size_t* out_budget)
{
    struct net_buf* buf = net_buf_alloc(g_protocol_config.response_pool, K_NO_WAIT);
    if (buf == NULL)
    {
        return -ENOMEM;
    }
    net_buf_reserve(buf, g_protocol_config.headroom);

    size_t tail = net_buf_tailroom(buf);
    ASSERT(tail >= g_protocol_config.tailroom, "Provided buffers are too small")

    *out_buf = buf;
    *out_budget = tail - g_protocol_config.tailroom;
    return SUCCESS;
}

/**
 * @brief Allocate, encode, and emit a response
 *
 * @details Calls the supplied encoding function to populate the buf, then hands
 * the buf to the configured response callback.
 */
static int emit_response(uint32_t session_id, int (*encode_fn)(zcbor_state_t*, void*), void* encode_context)
{
    struct net_buf* buf = NULL;
    size_t budget = 0;

    int ret = alloc_response_buf(&buf, &budget);
    if (ret != SUCCESS)
    {
        return ret;
    }

    uint8_t* start = net_buf_tail(buf);
    zcbor_state_t enc[ZCBOR_STATES];
    zcbor_new_encode_state(enc, ARRAY_SIZE(enc), start, budget, 1);

    ret = encode_fn(enc, encode_context);
    if (ret != SUCCESS)
    {
        net_buf_unref(buf);
        return ret;
    }

    size_t encoded = enc[0].payload - start;
    net_buf_add(buf, encoded);

    g_protocol_config.response_cb(session_id, buf);
    return SUCCESS;
}

static int encode_ok(zcbor_state_t* enc, void* ctx)
{
    ARG_UNUSED(ctx);
    return stow_serde_encode_ok(enc);
}

static int encode_version(zcbor_state_t* enc, void* ctx)
{
    ARG_UNUSED(ctx);
    return stow_serde_encode_version_response(enc);
}

static int encode_error(zcbor_state_t* enc, void* ctx)
{
    return stow_serde_encode_error(enc, ((struct encode_error_context*)ctx)->code);
}

static int encode_describe(zcbor_state_t* enc, void* ctx)
{
    struct encode_describe_context* describe_context = (struct encode_describe_context*)ctx;
    return stow_serde_encode_describe_response(
        enc, describe_context->next_item_id, describe_context->has_more, describe_context->chunk,
        describe_context->chunk_len);
}

static int encode_get_response(zcbor_state_t* enc, void* ctx)
{
    struct encode_value_context* value_context = (struct encode_value_context*)ctx;
    return stow_serde_encode_get_response(enc, value_context->id, value_context->value);
}

static int encode_update(zcbor_state_t* enc, void* ctx)
{
    struct encode_value_context* value_context = (struct encode_value_context*)ctx;
    return stow_serde_encode_update(enc, value_context->id, value_context->value);
}

static int encode_multi_get_response(zcbor_state_t* enc, void* ctx)
{
    struct encode_multi_get_context* multi_get_context = (struct encode_multi_get_context*)ctx;
    return stow_serde_encode_multi_get_response(
        enc, multi_get_context->ids, multi_get_context->values, multi_get_context->count);
}

static void emit_ok(uint32_t session_id)
{
    int ret = emit_response(session_id, encode_ok, NULL);
    if (ret != SUCCESS)
    {
        LOG_ERR("Failed to emit OK to session %u (%d)", session_id, ret);
    }
}

static void emit_error_code(uint32_t session_id, int err)
{
    enum stow_error_code code;

    switch (err)
    {
        case -EBADMSG:
            code = STOW_ERR_MALFORMED_MSG;
            break;
        case -ENOTSUP:
            code = STOW_ERR_UNKNOWN_MSG;
            break;
        case -EINVAL:
            code = STOW_ERR_INVALID_ITEM;
            break;
        case -ENOMEM:
            code = STOW_ERR_OUT_OF_MEMORY;
            break;
        case -EACCES:
            code = STOW_ERR_PERMISSION_DENIED;
            break;
        default:
            code = STOW_ERR_UNKNOWN;
            break;
    }

    emit_error(session_id, code);
}

static void emit_error(uint32_t session_id, enum stow_error_code code)
{
    struct encode_error_context ctx = { .code = code };
    int ret = emit_response(session_id, encode_error, &ctx);
    if (ret != SUCCESS)
    {
        LOG_ERR("Failed to emit Error to session %u (code %u, ret %d)", session_id, (uint32_t)code, ret);
    }
}

static void emit_multi_get_response(
    uint32_t session_id, const enum stow_item_id* ids, const data_value_t* values, uint8_t count)
{
    struct encode_multi_get_context ctx = { .ids = ids, .values = values, .count = count };
    int ret = emit_response(session_id, encode_multi_get_response, &ctx);
    if (ret != SUCCESS)
    {
        LOG_ERR("Failed to emit Multi-Get Response to session %u (%d)", session_id, ret);
    }
}

static void emit_version_response(uint32_t session_id)
{
    int ret = emit_response(session_id, encode_version, NULL);
    if (ret != SUCCESS)
    {
        LOG_ERR("Failed to emit Version Response to session %u (%d)", session_id, ret);
    }
}

static void emit_get_response(uint32_t session_id, enum stow_item_id id, data_value_t value)
{
    struct encode_value_context ctx = { .id = id, .value = value };
    int ret = emit_response(session_id, encode_get_response, &ctx);
    if (ret != SUCCESS)
    {
        LOG_ERR("Failed to emit Get Response to session %u (%d)", session_id, ret);
    }
}

static void emit_update(uint32_t session_id, enum stow_item_id id, data_value_t value)
{
    struct encode_value_context ctx = { .id = id, .value = value };
    int ret = emit_response(session_id, encode_update, &ctx);
    if (ret != SUCCESS)
    {
        LOG_ERR("Failed to emit Update to session %u (%d)", session_id, ret);
    }
}

static void emit_describe_chunk(uint32_t session_id, uint32_t start_id)
{
    int ret = SUCCESS;
    zcbor_state_t inner[ZCBOR_STATES];
    zcbor_new_encode_state(
        inner, ARRAY_SIZE(inner), describe_chunk_staging_buffer, sizeof(describe_chunk_staging_buffer), 1);

    uint32_t next_id = 0;
    // Ignore result of describe. Out of memory conditions are expected as the protocol dictates chunking the full
    // description
    (void)stow_describe(start_id, inner, &next_id);

    size_t chunk_len = inner[0].payload - describe_chunk_staging_buffer;
    bool has_more = (next_id < STOW_ID_COUNT);

    struct encode_describe_context ctx = {
        .chunk = describe_chunk_staging_buffer,
        .chunk_len = chunk_len,
        .next_item_id = next_id,
        .has_more = has_more,
    };
    ret = emit_response(session_id, encode_describe, &ctx);
    if (ret != SUCCESS)
    {
        LOG_ERR("Failed to emit Describe Response to session %u (%d)", session_id, ret);
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

static void handle_version(uint32_t session_id) { emit_version_response(session_id); }

static void handle_describe(uint32_t session_id, enum stow_item_id start_id)
{
    emit_describe_chunk(session_id, (uint32_t)start_id);
}

static void handle_get(uint32_t session_id, struct protocol_session* session, enum stow_item_id id)
{
    data_value_t value = { 0 };
    int ret = STOW_GET(session->roles, id, &value);
    if (ret != SUCCESS)
    {
        emit_error_code(session_id, ret);
        NOT_REFERENCED(value);
        return;
    }
    emit_get_response(session_id, id, value);
    STOW_RELEASE(id, &value);
}

static void handle_set(uint32_t session_id, struct protocol_session* session, const struct stow_serde_request* request)
{
    int ret = STOW_SET(session->roles, request->item_id, request->value);
    if (ret == SUCCESS)
    {
        emit_ok(session_id);
    }
    else
    {
        emit_error_code(session_id, ret);
    }
}

static void handle_subscribe(uint32_t session_id, struct protocol_session* session, enum stow_item_id id)
{
    if (atomic_test_bit(session->subscriptions, (int)id))
    {
        emit_ok(session_id);
        return;
    }

    if (item_subscription_counts[id] == 0)
    {
        item_subscription_counts[id]++;
        atomic_set_bit(session->subscriptions, (int)id);
        int ret = stow_subscribe(session->roles, id, &stow_protocol_subscription);
        if (ret != SUCCESS)
        {
            atomic_clear_bit(session->subscriptions, (int)id);
            item_subscription_counts[id]--;
            emit_error_code(session_id, ret);
            return;
        }
    }
    else
    {
        if (session->roles != STOW_ROLE_INTERNAL &&
            (g_stow_const_metadata[id].permissions.read_permissions & session->roles) == 0)
        {
            emit_error(session_id, STOW_ERR_PERMISSION_DENIED);
            return;
        }
        item_subscription_counts[id]++;
        atomic_set_bit(session->subscriptions, (int)id);
    }
    emit_ok(session_id);
}

static void handle_unsubscribe(uint32_t session_id, struct protocol_session* session, enum stow_item_id id)
{
    if (atomic_test_bit(session->subscriptions, (int)id))
    {
        remove_subscription(session, id);
    }
    emit_ok(session_id);
}

static void handle_multi_get(
    uint32_t session_id, struct protocol_session* session, const struct stow_serde_request* request)
{
    data_value_t values[CONFIG_STOW_PROTOCOL_MULTI_MAX_ITEMS] = { 0 };
    bool got[CONFIG_STOW_PROTOCOL_MULTI_MAX_ITEMS] = { false };
    bool failed = false;

    for (uint8_t i = 0; i < request->multi_count; i++)
    {
        data_value_t* value_ptr = &values[i];
        int ret = STOW_GET(session->roles, request->multi_ids[i], value_ptr);
        if (ret != SUCCESS)
        {
            emit_error_code(session_id, ret);
            NOT_REFERENCED(value_ptr);
            failed = true;
            break;
        }
        got[i] = true;

        // Values will be freed in a loop after sending the response
        PASS_OWNERSHIP(value_ptr)
    }

    if (!failed)
    {
        emit_multi_get_response(session_id, request->multi_ids, values, request->multi_count);
    }

    for (uint8_t i = 0; i < request->multi_count; i++)
    {
        if (got[i])
        {
            data_value_t* value_ptr = &values[i];
            STOW_RELEASE(request->multi_ids[i], value_ptr);
        }
    }
}

static void handle_multi_set(
    uint32_t session_id, struct protocol_session* session, const struct stow_serde_request* request)
{
    for (uint8_t i = 0; i < request->multi_count; i++)
    {
        int ret = STOW_SET(session->roles, request->multi_ids[i], request->multi_values[i]);
        if (ret != SUCCESS)
        {
            emit_error_code(session_id, ret);
            return;
        }
    }
    emit_ok(session_id);
}

static void dispatch_request(
    uint32_t session_id, struct protocol_session* session, const struct stow_serde_request* request)
{
    switch (request->message_code)
    {
        case STOW_MSG_VERSION:
            handle_version(session_id);
            break;
        case STOW_MSG_DESCRIBE:
            handle_describe(session_id, request->item_id);
            break;
        case STOW_MSG_GET:
            handle_get(session_id, session, request->item_id);
            break;
        case STOW_MSG_SET:
            handle_set(session_id, session, request);
            break;
        case STOW_MSG_SUBSCRIBE:
            handle_subscribe(session_id, session, request->item_id);
            break;
        case STOW_MSG_UNSUBSCRIBE:
            handle_unsubscribe(session_id, session, request->item_id);
            break;
        case STOW_MSG_MULTI_GET:
            handle_multi_get(session_id, session, request);
            break;
        case STOW_MSG_MULTI_SET:
            handle_multi_set(session_id, session, request);
            break;
        default:
            ASSERT(false, "Unexpected message code");
            break;
    }
}

static void handle_rx(const struct stow_protocol_event_payload* payload)
{
    struct protocol_session* session = get_session(payload->rx.session_id);
    if (session == NULL)
    {
        LOG_WRN("RX for unknown session %u", payload->rx.session_id);
        return;
    }
    session->roles = payload->rx.roles;

    if (payload->rx.buf == NULL || payload->rx.buf->len == 0)
    {
        emit_error(payload->rx.session_id, STOW_ERR_MALFORMED_MSG);
        return;
    }

    const uint8_t* cursor = payload->rx.buf->data;
    const uint8_t* const end = cursor + payload->rx.buf->len;

    while (cursor < end)
    {
        zcbor_state_t dec[ZCBOR_STATES];
        zcbor_new_decode_state(dec, ARRAY_SIZE(dec), cursor, (size_t)(end - cursor), 1, NULL, 0);

        struct stow_serde_request request = { 0 };
        int ret = stow_serde_decode_request(dec, &request);
        if (ret != SUCCESS)
        {
            LOG_WRN("Request decode failed for session %u (%d)", payload->rx.session_id, ret);
            emit_error_code(payload->rx.session_id, ret);
            stow_serde_release_request(&request);
            break;
        }

        dispatch_request(payload->rx.session_id, session, &request);
        stow_serde_release_request(&request);
        cursor = dec[0].payload;
    }
}

static void handle_update(event_t* event)
{
    const struct stow_update_event_payload* payload = (const struct stow_update_event_payload*)event->data.buf;
    const enum stow_item_id id = (enum stow_item_id)payload->metadata->id;

    for (size_t i = 0; i < ARRAY_SIZE(sessions); i++)
    {
        struct protocol_session* session = &sessions[i];
        if (!session->in_use)
        {
            continue;
        }

        if (!atomic_test_bit(session->subscriptions, (int)id))
        {
            continue;
        }

        if (session->roles != STOW_ROLE_INTERNAL &&
            (payload->metadata->permissions.read_permissions & session->roles) == 0)
        {
            // Auto-revoke defensively in case the role handler missed it.
            remove_subscription(session, id);
            continue;
        }

        emit_update(session->session_id, id, payload->value_copy);
    }
}

static void handle_session_roles(const struct stow_protocol_event_payload* payload)
{
    struct protocol_session* session = get_session(payload->session_roles.session_id);
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
        if (session->roles != STOW_ROLE_INTERNAL &&
            (g_stow_const_metadata[id].permissions.read_permissions & session->roles) == 0)
        {
            remove_subscription(session, (enum stow_item_id)id);
        }
    }
}

static void handle_session_close(const struct stow_protocol_event_payload* payload)
{
    struct protocol_session* session = get_session(payload->session_close.session_id);
    if (session == NULL)
    {
        LOG_WRN("session_closed for unknown session %u", payload->session_close.session_id);
        return;
    }

    for (size_t id = 0; id < STOW_ID_COUNT; id++)
    {
        if (atomic_test_bit(session->subscriptions, (int)id))
        {
            remove_subscription(session, (enum stow_item_id)id);
        }
    }

    session->in_use = false;
    session->session_id = 0;
}

static void protocol_thread(void* p1, void* p2, void* p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    while (true)
    {
        event_t* event = NULL;
        k_msgq_get(&stow_protocol_queue, (void*)&event, K_FOREVER);

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
                case STOW_PROTOCOL_SESSION_CLOSE:
                    handle_session_close(p);
                    break;
            }
        }
        else
        {
            LOG_WRN("Unexpected event id %u", event->type->id);
        }

        EVENT_UNREF(&event);
    }
}

//**********************************************************
//* Public Function Definitions
//**********************************************************

int stow_protocol_init(const struct stow_protocol_config* config)
{
    if (config == NULL || config->response_cb == NULL || config->response_pool == NULL)
    {
        return -EINVAL;
    }

    if (CHECK_FLAG(stow_protocol_flags, INITIALIZED))
    {
        return -EALREADY;
    }

    g_protocol_config = *config;
    SET_FLAG(stow_protocol_flags, INITIALIZED);

    memset(sessions, 0, sizeof(sessions));
    memset(item_subscription_counts, 0, sizeof(item_subscription_counts));

    stow_protocol_thread_id = k_thread_create(
        &stow_protocol_thread, stow_protocol_stack, K_THREAD_STACK_SIZEOF(stow_protocol_stack), protocol_thread, NULL,
        NULL, NULL, CONFIG_STOW_PROTOCOL_THREAD_PRIORITY, 0, K_NO_WAIT);
    k_thread_name_set(stow_protocol_thread_id, "stow_protocol");

    return SUCCESS;
}

int stow_protocol_session_open(uint32_t session_id, stow_role_t roles)
{
    ASSERT(CHECK_FLAG(stow_protocol_flags, INITIALIZED), "Not initialized");

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
            break;
        }
    }

    if (free_slot == NULL)
    {
        irq_unlock(key);
        return -ENOMEM;
    }

    free_slot->session_id = session_id;
    free_slot->roles = roles;
    for (int bit = 0; bit < STOW_ID_COUNT; bit++)
    {
        atomic_clear_bit(free_slot->subscriptions, bit);
    }
    free_slot->in_use = true;

    irq_unlock(key);
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

int stow_protocol_session_closed(uint32_t session_id)
{
    ASSERT(CHECK_FLAG(stow_protocol_flags, INITIALIZED), "Not initialized");

    event_t* event = NULL;
    struct stow_protocol_event_payload* payload = NULL;
    int ret = alloc_protocol_event(STOW_PROTOCOL_SESSION_CLOSE, &event, &payload);
    if (ret != SUCCESS)
    {
        return ret;
    }

    payload->session_close.session_id = session_id;

    return push_event(event);
}

int stow_protocol_handle_rx(uint32_t session_id, stow_role_t roles, struct net_buf* buf)
{
    ASSERT(CHECK_FLAG(stow_protocol_flags, INITIALIZED), "Not initialized");

    if (buf == NULL)
    {
        return -EINVAL;
    }

    event_t* event = NULL;
    struct stow_protocol_event_payload* payload = NULL;
    int ret = alloc_protocol_event(STOW_PROTOCOL_RX, &event, &payload);
    if (ret != SUCCESS)
    {
        return ret;
    }

    payload->rx.session_id = session_id;
    payload->rx.roles = roles;
    payload->rx.buf = net_buf_ref(buf);

    return push_event(event);
}
