/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Stow client
 *
 * @details Reads, writes and subscribes to a peer's Stow over the @ref stow_transport contract. A client endpoint is
 * built against the items a peer is expected to have; each session a transport opens on it is a peer, checked against
 * the peer's description when the session opens.
 *
 * @author Demizo (demizodemazo@gmail.com)
 *
 */

#pragma once

#include <gantry/event.h>
#include <gantry/memory.h>
#include <gantry/stow/stow_transport.h>
#include <gantry/stow/types/stow_types.h>
#include <stddef.h>
#include <stdint.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/iterable_sections.h>

/**
 * @addtogroup stow_client Stow Client
 * @{
 */

//**********************************************************
//* Definitions
//**********************************************************

/**
 * @brief Marks a result that names no failing entry
 */
#define STOW_CLIENT_NO_INDEX UINT16_MAX

/**
 * @brief Event ID for Updates from a peer
 */
#define EVENT_ID_STOW_CLIENT_UPDATE 3

/**
 * @brief Event ID for the results of asynchronous requests
 */
#define EVENT_ID_STOW_CLIENT_RESULT 4

/**
 * @brief Update event, with a @ref stow_client_update_payload
 */
DECLARE_EVENT_TYPE(stow_client_update_event);

/**
 * @brief Result event, with a @ref stow_client_result_payload
 */
DECLARE_EVENT_TYPE(stow_client_result_event);

//**********************************************************
//* Typedefs, Enums, and Structs
//**********************************************************

/**
 * @brief An item the client expects a peer to have
 */
struct stow_client_item
{
    const char* name;                            /**< Name of the item */
    enum stow_item_type type;                    /**< Expected type */
    const struct stow_item_interface* interface; /**< Codec for the item's values */
    union stow_constraints constraints;          /**< Expected constraints */
};

/**
 * @brief The items a client expects a peer to have, generated from the peer's schema
 */
struct stow_client_schema
{
    const struct stow_client_item* items; /**< Expected items, indexed by the generated item IDs */
    uint16_t item_count;                  /**< Number of expected items */
};

/**
 * @brief A client endpoint, defined with @ref STOW_CLIENT_ENDPOINT_DEFINE
 *
 * @details Transports open sessions on @ref stow_client_endpoint.endpoint, one peer per session.
 */
struct stow_client_endpoint
{
    struct stow_endpoint endpoint;           /**< The Stow side of the transport contract */
    const struct stow_client_schema* schema; /**< Items each peer is expected to have */
    uint32_t timeout_ms;                     /**< Request timeout, or 0 for CONFIG_STOW_CLIENT_TIMEOUT_MS */
};

/**
 * @brief Whether a connected peer has an expected item
 */
enum stow_client_availability
{
    STOW_CLIENT_MISSING = 0,      /**< The peer has no item with the expected name */
    STOW_CLIENT_INCOMPATIBLE = 1, /**< The peer's item differs in type or constraints */
    STOW_CLIENT_AVAILABLE = 2,    /**< The peer's item matches */
};

/**
 * @brief A peer: one session on a client endpoint
 */
struct stow_client_peer;

/**
 * @brief An item and its value
 */
struct stow_client_value
{
    uint16_t id;        /**< Generated item ID */
    data_value_t value; /**< The item's value */
};

/**
 * @brief Payload of a @ref stow_client_update_event
 *
 * @details The event owns the value and a reference to the peer.
 */
struct stow_client_update_payload
{
    struct stow_client_peer* peer;  /**< The peer */
    struct stow_client_value value; /**< The item and its new value */
};

/**
 * @brief Payload of a @ref stow_client_result_event, the outcome of an asynchronous request
 *
 * @details The event owns the values and a reference to the peer.
 */
struct stow_client_result_payload
{
    struct stow_client_peer* peer;     /**< The peer */
    int err;                           /**< SUCCESS or a negative errno, as for the blocking call */
    uint16_t index;                    /**< Entry that failed as it was applied, or @ref STOW_CLIENT_NO_INDEX */
    uint16_t count;                    /**< Number of values */
    struct stow_client_value values[]; /**< Values of a Get */
};

/**
 * @brief Called on the client thread with the @ref stow_client_result_event of an asynchronous request
 *
 * @details Keep the event with @ref EVENT_REF; the client releases its own reference when this returns.
 */
typedef void (*stow_client_cb_t)(event_t* event, void* user_data);

/**
 * @brief What the peer reported in its Hello Response
 */
struct stow_client_peer_info
{
    uint32_t max_message; /**< Largest request the peer accepts, in bytes */
    stow_role_t roles;    /**< Roles the session holds, named by the generated role constants */
};

/**
 * @brief Hooks called on the client thread, registered with @ref STOW_CLIENT_CB_DEFINE
 *
 * @details Any hook may be NULL. The peer is borrowed for the duration of a hook; take a reference to keep it.
 */
struct stow_client_cb
{
    /**
     * @brief Verification of a peer finished
     *
     * @details On failure the peer stays unusable, and while its session is open the app closes it through its
     * transport.
     * Missing or incompatible items do not fail verification.
     *
     * @param peer The peer
     * @param err SUCCESS when requests may be made, otherwise why verification failed
     */
    void (*connected)(struct stow_client_peer* peer, int err);

    /**
     * @brief The session of a connected peer closed, after every pending request failed
     *
     * @param peer The peer
     */
    void (*disconnected)(struct stow_client_peer* peer);

    /**
     * @brief A subscribed item changed on a peer
     *
     * @details Every hook gets the same event. Keep it with @ref EVENT_REF; the client releases its own reference
     * when the hooks return.
     *
     * @param event A @ref stow_client_update_event
     */
    void (*update)(event_t* event);
};

/**
 * @brief Statically register client hooks
 *
 * @param _name Name of the hook instance
 */
#define STOW_CLIENT_CB_DEFINE(_name) static const STRUCT_SECTION_ITERABLE(stow_client_cb, _name)

/** @cond INTERNAL_HIDDEN */
int z_stow_client_session_opened(
    const struct stow_endpoint* endpoint, uint32_t session_id, stow_role_t roles, struct stow_transport* transport);
void z_stow_client_session_closed(const struct stow_endpoint* endpoint, uint32_t session_id);
void z_stow_client_receive(const struct stow_endpoint* endpoint, uint32_t session_id, struct net_buf* buf);
/** @endcond */

/**
 * @brief Define a client endpoint for peers with the given schema
 *
 * @details Opening a session fails with -EINVAL for an incomplete transport, -EALREADY when the session is open and
 * -ENOMEM when every one of the CONFIG_STOW_CLIENT_MAX_PEERS peers is open or still referenced, or no memory is free.
 *
 * @param _name Name of the @ref stow_client_endpoint
 * @param _schema The generated schema, for example `&device_stow_client_schema`
 * @param _timeout_ms Request timeout, or 0 for CONFIG_STOW_CLIENT_TIMEOUT_MS
 */
#define STOW_CLIENT_ENDPOINT_DEFINE(_name, _schema, _timeout_ms) \
    const struct stow_client_endpoint _name = {                         \
        .endpoint =                                                     \
            {                                                           \
                .session_opened = z_stow_client_session_opened,         \
                .session_closed = z_stow_client_session_closed,         \
                .receive = z_stow_client_receive,                       \
            },                                                          \
        .schema = (_schema),                                            \
        .timeout_ms = (_timeout_ms),                                    \
    }

//**********************************************************
//* Functions
//**********************************************************

/**
 * @brief Take a reference to a peer, keeping it valid after a hook or callback returns
 *
 * @details Take it while the peer is borrowed or already referenced.
 *
 * @param peer The peer
 *
 * @return @p peer
 * @acquires stow_client_peer return
 */
struct stow_client_peer* stow_client_peer_ref(struct stow_client_peer* peer);

/**
 * @brief Release a reference to a peer and clear the pointer
 *
 * @param[in,out] peer The reference to release
 * @releases stow_client_peer peer
 */
void stow_client_peer_unref(struct stow_client_peer** peer);

/**
 * @brief Get the endpoint a peer's session is on
 *
 * @param peer The peer
 *
 * @return The endpoint
 */
const struct stow_client_endpoint* stow_client_peer_endpoint(struct stow_client_peer* peer);

/**
 * @brief Get the ID the transport gave a peer's session
 *
 * @param peer The peer
 *
 * @return The session ID
 */
uint32_t stow_client_peer_session_id(struct stow_client_peer* peer);

/**
 * @brief Read what a connected peer reported in its Hello Response
 *
 * @param peer The peer
 * @param[out] info_out Populated with the peer's limit and the session's roles
 *
 * @return SUCCESS when the peer is connected
 * @return -ENOTCONN when it is not
 */
int stow_client_peer_info(struct stow_client_peer* peer, struct stow_client_peer_info* info_out);

/**
 * @brief Check whether a connected peer has an expected item
 *
 * @param peer The peer
 * @param id Generated item ID
 *
 * @return The item's availability, @ref STOW_CLIENT_MISSING while the peer is not connected
 */
enum stow_client_availability stow_client_availability(struct stow_client_peer* peer, uint16_t id);

/**
 * @brief Get the values of one or more items, blocking until the response
 *
 * @details Must not be called from an interrupt or the client thread. Release each value with
 * @ref stow_client_release.
 *
 * @param peer The peer
 * @param ids Generated item IDs
 * @param count Number of items
 * @param[out] values_out Each item and its value, in the order requested
 *
 * @return SUCCESS when every value was read
 * @return -EINVAL when no or more than UINT16_MAX items are given, or an ID is unknown
 * @return -ENOTCONN when the peer is not connected, or its session closed before the response
 * @return -ENOTSUP when an item is missing or incompatible on the peer
 * @return -EMSGSIZE when the request exceeds the smaller of the peer's max_message and the client's buffers
 * @return -EBUSY when no request buffer was free, or the transport or the client's queue refused the request
 * @return -ETIMEDOUT when no response arrived in time
 * @return -ENOMSG when the response was malformed or did not match the request
 * @return -ENOBUFS when the client had no memory for the request or response
 * @return -EWOULDBLOCK when called from an interrupt or the client thread
 * @return -EBADMSG, -EPROTO, -ENOENT, -ENOMEM, -EACCES, -E2BIG or -EIO when the peer answered with an Error
 * @acquires memory values_out on-success
 */
int stow_client_get(
    struct stow_client_peer* peer, const uint16_t* ids, size_t count, struct stow_client_value* values_out);

/**
 * @brief Set the values of one or more items, blocking until the response
 *
 * @details Items are set in order and the first failure stops the rest. Which entry failed is not reported; use
 * @ref stow_client_set_async when it matters.
 *
 * @param peer The peer
 * @param ids Generated item IDs
 * @param values Value of each item
 * @param count Number of items
 *
 * @return SUCCESS when every value was set
 * @return -EINVAL when a value breaks the item's constraints, locally or on the peer
 * @return -EALREADY when a trust-on-first-use item is already set
 * @return a codec's error when a value cannot be encoded
 * @return the other errors of @ref stow_client_get
 */
int stow_client_set(struct stow_client_peer* peer, const uint16_t* ids, const data_value_t* values, size_t count);

/**
 * @brief Subscribe to one or more items, blocking until the response
 *
 * @details Updates arrive through the @ref stow_client_cb.update hooks. They are best effort, so Get an item after
 * subscribing and after reconnecting, and poll items where a stale value matters. Subscriptions end with the session.
 *
 * @param peer The peer
 * @param ids Generated item IDs
 * @param count Number of items
 *
 * @return the results of @ref stow_client_get
 */
int stow_client_subscribe(struct stow_client_peer* peer, const uint16_t* ids, size_t count);

/**
 * @brief Unsubscribe from one or more items, blocking until the response
 *
 * @param peer The peer
 * @param ids Generated item IDs
 * @param count Number of items
 *
 * @return the results of @ref stow_client_get
 */
int stow_client_unsubscribe(struct stow_client_peer* peer, const uint16_t* ids, size_t count);

/**
 * @brief Get the values of one or more items without blocking
 *
 * @param peer The peer
 * @param ids Generated item IDs
 * @param count Number of items
 * @param cb Called with the result event, not NULL
 * @param user_data Passed to @p cb
 *
 * @return SUCCESS when @p cb will be called
 * @return -EAGAIN when no request slot or tag is free
 * @return -ENOBUFS when no event is free for the result
 * @return the errors of @ref stow_client_get found before sending
 */
int stow_client_get_async(
    struct stow_client_peer* peer, const uint16_t* ids, size_t count, stow_client_cb_t cb, void* user_data);

/**
 * @brief Set the values of one or more items without blocking
 *
 * @param peer The peer
 * @param ids Generated item IDs
 * @param values Value of each item, encoded before this returns
 * @param count Number of items
 * @param cb Called with the result event, not NULL
 * @param user_data Passed to @p cb
 *
 * @return the results of @ref stow_client_get_async
 */
int stow_client_set_async(
    struct stow_client_peer* peer, const uint16_t* ids, const data_value_t* values, size_t count, stow_client_cb_t cb,
    void* user_data);

/**
 * @brief Subscribe to one or more items without blocking
 *
 * @param peer The peer
 * @param ids Generated item IDs
 * @param count Number of items
 * @param cb Called with the result event, not NULL
 * @param user_data Passed to @p cb
 *
 * @return the results of @ref stow_client_get_async
 */
int stow_client_subscribe_async(
    struct stow_client_peer* peer, const uint16_t* ids, size_t count, stow_client_cb_t cb, void* user_data);

/**
 * @brief Unsubscribe from one or more items without blocking
 *
 * @param peer The peer
 * @param ids Generated item IDs
 * @param count Number of items
 * @param cb Called with the result event, not NULL
 * @param user_data Passed to @p cb
 *
 * @return the results of @ref stow_client_get_async
 */
int stow_client_unsubscribe_async(
    struct stow_client_peer* peer, const uint16_t* ids, size_t count, stow_client_cb_t cb, void* user_data);

/**
 * @brief Release a value returned by @ref stow_client_get or kept with @ref stow_client_ref
 *
 * @param endpoint Endpoint of the peer the value came from
 * @param[in,out] value The item and value to release
 * @releases memory value
 */
void stow_client_release(const struct stow_client_endpoint* endpoint, struct stow_client_value* value);

/**
 * @brief Convenience macro for @ref stow_client_release with debug tracing
 *
 * @releases memory value
 */
#define STOW_CLIENT_RELEASE(endpoint, value) TRACE_WRAP_VOID(stow_client_release(endpoint, value))

/**
 * @brief Add a reference to a value; release it with @ref stow_client_release
 *
 * @param endpoint Endpoint of the peer the value came from
 * @param[in] value The item and value to keep
 * @acquires memory value
 */
void stow_client_ref(const struct stow_client_endpoint* endpoint, struct stow_client_value* value);

/**
 * @brief Convenience macro for @ref stow_client_ref with debug tracing
 *
 * @acquires memory value
 */
#define STOW_CLIENT_REF(endpoint, value) TRACE_WRAP_VOID(stow_client_ref(endpoint, value))

/**
 * @brief Get the value of one item, see @ref stow_client_get
 *
 * @acquires memory value_out on-success
 */
static inline int stow_client_get_one(struct stow_client_peer* peer, uint16_t id, struct stow_client_value* value_out)
{
    return stow_client_get(peer, &id, 1, value_out);
}

/**
 * @brief Set the value of one item, see @ref stow_client_set
 */
static inline int stow_client_set_one(struct stow_client_peer* peer, uint16_t id, data_value_t value)
{
    return stow_client_set(peer, &id, &value, 1);
}

/**
 * @brief Subscribe to one item, see @ref stow_client_subscribe
 */
static inline int stow_client_subscribe_one(struct stow_client_peer* peer, uint16_t id)
{
    return stow_client_subscribe(peer, &id, 1);
}

/**
 * @brief Unsubscribe from one item, see @ref stow_client_unsubscribe
 */
static inline int stow_client_unsubscribe_one(struct stow_client_peer* peer, uint16_t id)
{
    return stow_client_unsubscribe(peer, &id, 1);
}

/**
 * @brief Get the value of one item without blocking, see @ref stow_client_get_async
 */
static inline int stow_client_get_one_async(
    struct stow_client_peer* peer, uint16_t id, stow_client_cb_t cb, void* user_data)
{
    return stow_client_get_async(peer, &id, 1, cb, user_data);
}

/**
 * @brief Set the value of one item without blocking, see @ref stow_client_set_async
 */
static inline int stow_client_set_one_async(
    struct stow_client_peer* peer, uint16_t id, data_value_t value, stow_client_cb_t cb, void* user_data)
{
    return stow_client_set_async(peer, &id, &value, 1, cb, user_data);
}

/**
 * @brief Subscribe to one item without blocking, see @ref stow_client_subscribe_async
 */
static inline int stow_client_subscribe_one_async(
    struct stow_client_peer* peer, uint16_t id, stow_client_cb_t cb, void* user_data)
{
    return stow_client_subscribe_async(peer, &id, 1, cb, user_data);
}

/**
 * @brief Unsubscribe from one item without blocking, see @ref stow_client_unsubscribe_async
 */
static inline int stow_client_unsubscribe_one_async(
    struct stow_client_peer* peer, uint16_t id, stow_client_cb_t cb, void* user_data)
{
    return stow_client_unsubscribe_async(peer, &id, 1, cb, user_data);
}

/**
 * @}
 */
