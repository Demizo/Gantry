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
//* Typedefs, Enums, and Structs
//**********************************************************

/**
 * @brief The protocol server's side of the transport contract
 *
 * @details Opening a session fails with -EINVAL for a transport stow_transport_usable() rejects, -EALREADY for an ID
 * in use and -ENOMEM when every slot is taken.
 */
extern const struct stow_endpoint stow_protocol_endpoint;

//**********************************************************
//* Functions
//**********************************************************

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
 * @return SUCCESS when the change is queued, or no such session is open
 * @return -ENOBUFS when the protocol queue is full
 */
int stow_protocol_session_set_roles(uint32_t session_id, stow_role_t roles);

/**
 * @}
 */
