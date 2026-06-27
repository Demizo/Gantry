/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Stow protocol
 *
 * @details Owns a dedicated thread that bridges external sessions
 * to the internal Stow API. This protocol is designed to be wrapped in an arbitrary
 * high-level protocol. It will output Stow responses in Zephyr net_bufs with a configurable
 * amount of reserved headroom and tailroom so the higher level protocol can wrap the
 * Stow protocol.
 *
 * @author Demizo (demizodemazo@gmail.com)
 *
 */

#pragma once

#include <gantry/event.h>
#include <gantry/stow/types/stow_types.h>
#include <stddef.h>
#include <stdint.h>
#include <zephyr/kernel.h>
#include <zephyr/net_buf.h>

/**
 * @addtogroup stow_protocol Stow Protocol
 * @{
 */

//**********************************************************
//* Definitions
//**********************************************************

/**
 * @brief Event ID for stow protocol operations
 */
#define EVENT_ID_STOW_PROTOCOL 2

/**
 * @brief Stow protocol event declaration
 */
DECLARE_EVENT_TYPE(stow_protocol_event);

//**********************************************************
//* Typedefs, Enums, and Structs
//**********************************************************

/**
 * @brief Type of stow protocol event
 */
enum stow_protocol_event_type
{
    STOW_PROTOCOL_RX,           /**< Incoming CBOR payload from a session */
    STOW_PROTOCOL_SESSION_AUTH, /**< Update a session's authentication roles */
    STOW_PROTOCOL_SESSION_CLOSE /**< Close a session and its subscriptions */
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
            stow_role_t roles;   /**< Current role bitmask for the session */
            struct net_buf* buf; /**< Inner Stow CBOR payload (released by on_free) */
        } rx;
        struct
        {
            uint32_t session_id; /**< Session ID to update */
            stow_role_t roles;   /**< New role bitmask for the session */
        } session_roles;
        struct
        {
            uint32_t session_id; /**< Session ID to close */
        } session_close;
    };
};

/**
 * @brief Response callback invoked by the protocol thread
 *
 * @details Hands ownership of a response net_buf to the consumer.
 * The net_buf is allocated from the consumer-provided net_buf pool with
 * the configured @ref stow_protocol_config::headroom and @ref stow_protocol_config::tailroom.
 * The consumer must call `net_buf_unref` after transmission.
 *
 * @param session_id Session that the response is destined for
 * @param buf net_buf containing the Stow response
 */
typedef void (*stow_protocol_response_cb_t)(uint32_t session_id, struct net_buf* buf);

/**
 * @brief Configuration for @ref stow_protocol_init
 */
struct stow_protocol_config
{
    stow_protocol_response_cb_t response_cb; /**< Response callback */
    struct net_buf_pool* response_pool;      /**< net_buf pool used for outgoing responses */
    size_t headroom;                         /**< Bytes to reserve at the head of the response */
    size_t tailroom;                         /**< Bytes to reserve at the tail of the response */
};

//**********************************************************
//* Functions
//**********************************************************

/**
 * @brief Initialize the Stow protocol module and start its thread
 *
 * @details Must be called after @ref stow_init.
 *
 * @param config Stow protocol configuration settings
 *
 * @return SUCCESS when the module is started
 * @return -EINVAL when the configuration is invalid
 * @return -EALREADY when the module is already initialized
 */
int stow_protocol_init(const struct stow_protocol_config* config);

/**
 * @brief Open a session
 *
 * @note Sessions should always have at least one role, otherwise they will have full access.
 *
 * @param session_id Unique identifier for the session
 * @param roles Initial role bitmask for the session
 *
 * @return SUCCESS when the session is opened
 * @return -EALREADY when a session with this ID is already open
 * @return -ENOMEM when the max number of sessions has already been reached
 */
int stow_protocol_session_open(uint32_t session_id, stow_role_t roles);

/**
 * @brief Update a session's roles
 *
 * @details Updates the session's roles. Active subscriptions that no longer satisfy
 * the new roles are automatically revoked.
 *
 * @note Sessions should always have at least one role, otherwise they will have full access.
 *
 * @param session_id The session to update
 * @param roles The new role bitmask
 *
 * @return SUCCESS when the event is queued
 * @return -ENOBUFS when the protocol queue is full
 * @return -ENOMEM when an event cannot be allocated
 */
int stow_protocol_session_set_roles(uint32_t session_id, stow_role_t roles);

/**
 * @brief Notify the protocol that a session has closed
 *
 * @details All of the session's subscriptions are removed and the session ID is made available. This process is not
 * synchonous, so the session ID will not be available immediately after this call. The result of @ref
 * stow_protocol_session_open indicates when the session ID is still active.
 *
 * @param session_id The session to tear down
 *
 * @return SUCCESS when session tear down is initiated
 * @return -ENOBUFS when the protocol queue is full
 * @return -ENOMEM when the tear down event cannot be allocated
 */
int stow_protocol_session_closed(uint32_t session_id);

/**
 * @brief Handle an incoming Stow message
 *
 * @details The protocol thread takes ownership of @p buf; the caller may
 * `net_buf_unref` the buf after this function returns.
 *
 * @param session_id The session that produced the message
 * @param roles The session's current role bitmask
 * @param buf net_buf containing the inner Stow CBOR payload
 *
 * @return SUCCESS when the event is queued
 * @return -EINVAL when @p buf is NULL
 * @return -ENOBUFS when the protocol queue is full
 * @return -ENOMEM when an event cannot be allocated
 */
int stow_protocol_handle_rx(uint32_t session_id, stow_role_t roles, struct net_buf* buf);

/**
 * @}
 */
