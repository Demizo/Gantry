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
 * to the internal Stow API. Each session is bound to the @ref stow_transport that opened it.
 * Responses are allocated from that transport's pool with its headroom and tailroom so the
 * transport can wrap the Stow protocol without copying.
 *
 * @author Demizo (demizodemazo@gmail.com)
 *
 */

#pragma once

#include <gantry/stow/stow_transport.h>
#include <gantry/stow/types/stow_types.h>
#include <stdint.h>

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

//**********************************************************
//* Typedefs, Enums, and Structs
//**********************************************************

/**
 * @brief The protocol server's side of the transport contract
 *
 * @details Opening a session fails with -EINVAL for an incomplete transport or a pool that is not fixed-size,
 * -EALREADY for an ID in use and -ENOMEM when every slot is taken.
 */
extern const struct stow_endpoint stow_protocol_endpoint;

//**********************************************************
//* Functions
//**********************************************************

/**
 * @brief Initialize the Stow protocol module and start its thread
 *
 * @details Must be called after @ref stow_init.
 *
 * @return SUCCESS when the module is started
 * @return -EALREADY when the module is already initialized
 */
int stow_protocol_init(void);

/**
 * @brief Update a session's roles
 *
 * @details Updates the session's roles. Active subscriptions that no longer satisfy
 * the new roles are automatically revoked.
 *
 * @note A session with no roles has no access to any item.
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
 * @}
 */
