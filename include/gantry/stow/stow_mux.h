/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Server and client Stow sessions over one carrier session
 *
 * @details For carrier implementers, so one link can carry a server session and a client session. Apps use the
 * carrier's calls.
 *
 * @author Demizo (demizodemazo@gmail.com)
 *
 */

#pragma once

#include <gantry/error.h>
#include <gantry/stow/stow_transport.h>
#include <gantry/stow/types/stow_types.h>
#include <stdint.h>
#include <sys/errno.h>
#include <zephyr/kernel.h>
#include <zephyr/net_buf.h>
#include <zephyr/sys/atomic.h>

/**
 * @addtogroup stow_protocol
 * @{
 */

//**********************************************************
//* Typedefs, Enums, and Structs
//**********************************************************

/**
 * @brief The Stow sessions on one carrier session
 */
struct stow_mux
{
    struct stow_transport* transport;   /**< Carrier transport both endpoints send through */
    uint32_t session_id;                /**< Session ID both endpoints use */
    const struct stow_endpoint* server; /**< Server endpoint, opened with the carrier, or NULL */
    stow_role_t roles;                  /**< Roles granted to the peer's client on the server */
#if defined(CONFIG_STOW_CLIENT) || defined(__DOXYGEN__)
    atomic_t open;              /**< Whether the carrier session is open */
    atomic_ptr_t client;        /**< Client endpoint whose session is open, or NULL */
    struct k_mutex client_lock; /**< Serializes opening and closing the client session */
#endif
};

//**********************************************************
//* Functions
//**********************************************************

/**
 * @brief Open a client session on the carrier session
 *
 * @note Not interrupt safe.
 *
 * @param mux The mux
 * @param client Client endpoint
 *
 * @return SUCCESS, -EINVAL for no endpoint, -ENOTCONN when the carrier session is not open, -EALREADY when a client
 * session is open, -ENOTSUP without CONFIG_STOW_CLIENT, or the client endpoint's error
 */
#if defined(CONFIG_STOW_CLIENT) || defined(__DOXYGEN__)
int stow_mux_open_client(struct stow_mux* mux, const struct stow_endpoint* client);
#else
static inline int stow_mux_open_client(struct stow_mux* mux, const struct stow_endpoint* client)
{
    ARG_UNUSED(mux);
    ARG_UNUSED(client);
    return -ENOTSUP;
}
#endif

/**
 * @brief Close the client session, if one is open
 *
 * @note Not interrupt safe.
 *
 * @param mux The mux
 */
#if defined(CONFIG_STOW_CLIENT) || defined(__DOXYGEN__)
void stow_mux_close_client(struct stow_mux* mux);
#else
static inline void stow_mux_close_client(struct stow_mux* mux) { ARG_UNUSED(mux); }
#endif

/**
 * @brief Route one incoming message to the server or the client session; the caller keeps its reference to @p buf
 *
 * @param mux The mux
 * @param buf The message
 */
#if defined(CONFIG_STOW_CLIENT) || defined(__DOXYGEN__)
void stow_mux_receive(struct stow_mux* mux, struct net_buf* buf);
#else
static inline void stow_mux_receive(struct stow_mux* mux, struct net_buf* buf)
{
    if (mux->server != NULL)
    {
        mux->server->receive(mux->server, mux->session_id, buf);
    }
}
#endif

/**
 * @brief Set up a mux
 *
 * @param mux The mux
 * @param transport Carrier transport
 * @param session_id Session ID both endpoints use
 * @param server Server endpoint, or NULL for none
 * @param roles Roles granted to the peer's client on the server
 */
static inline void stow_mux_init(
    struct stow_mux* mux, struct stow_transport* transport, uint32_t session_id, const struct stow_endpoint* server,
    stow_role_t roles)
{
    mux->transport = transport;
    mux->session_id = session_id;
    mux->server = server;
    mux->roles = roles;
#ifdef CONFIG_STOW_CLIENT
    atomic_clear(&mux->open);
    atomic_ptr_clear(&mux->client);
    k_mutex_init(&mux->client_lock);
#endif
}

/**
 * @brief The carrier session opened; open the server session, if there is a server
 *
 * @param mux The mux
 *
 * @return SUCCESS, or the server endpoint's error
 */
static inline int stow_mux_open(struct stow_mux* mux)
{
#ifdef CONFIG_STOW_CLIENT
    atomic_set(&mux->open, 1);
#endif
    if (mux->server == NULL)
    {
        return SUCCESS;
    }
    return mux->server->session_opened(mux->server, mux->session_id, mux->roles, mux->transport);
}

/**
 * @brief The carrier session closed; close the client and server sessions
 *
 * @note Not interrupt safe.
 *
 * @param mux The mux
 */
static inline void stow_mux_close(struct stow_mux* mux)
{
#ifdef CONFIG_STOW_CLIENT
    atomic_clear(&mux->open);
#endif
    stow_mux_close_client(mux);
    if (mux->server != NULL)
    {
        mux->server->session_closed(mux->server, mux->session_id);
    }
}

/**
 * @}
 */
