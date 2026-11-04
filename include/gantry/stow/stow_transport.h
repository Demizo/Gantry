/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Contract between Stow and the transport that carries its messages
 *
 * @details A transport delivers whole Stow messages to a @ref stow_endpoint and sends the ones it produces.
 *
 * @author Demizo (demizodemazo@gmail.com)
 *
 */

#pragma once

#include <gantry/error.h>
#include <gantry/ownership.h>
#include <gantry/stow/types/stow_types.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/errno.h>
#include <zephyr/net_buf.h>
#include <zephyr/sys/__assert.h>

/**
 * @addtogroup stow_protocol
 * @{
 */

//**********************************************************
//* Typedefs, Enums, and Structs
//**********************************************************

/**
 * @brief A transport that carries Stow messages for one or more sessions
 *
 * @details Transports embed this structure and recover their own state with `CONTAINER_OF`. A transport must deliver
 * any message up to the size of its pool's buffers.
 */
struct stow_transport
{
    /**
     * @brief Send one complete Stow message
     *
     * @details On success the transport takes ownership of @p buf. Otherwise ownership stays with the caller. A
     * transport that carries more than one session must not block; a transport that carries a single session may.
     *
     * @param transport The transport to send on
     * @param session_id Session the message belongs to
     * @param buf The message
     *
     * @return SUCCESS when the transport took the message, otherwise a negative errno
     */
    int (*send)(struct stow_transport* transport, uint32_t session_id, struct net_buf* buf);
    struct net_buf_pool* pool; /**< Fixed-size pool that outgoing messages are allocated from */
    size_t headroom;           /**< Bytes reserved at the head of each outgoing message */
    size_t tailroom;           /**< Bytes reserved at the tail of each outgoing message */
};

/**
 * @brief The Stow side of the transport contract
 *
 * @details Implementations embed this in a const instance structure and recover it with `CONTAINER_OF`.
 */
struct stow_endpoint
{
    /**
     * @brief A peer connected
     *
     * @param endpoint The endpoint
     * @param session_id Session identifier, unique within the endpoint
     * @param roles Roles granted to the peer
     * @param transport Transport that carries the session's messages
     *
     * @return SUCCESS when the session is open, otherwise a negative errno
     */
    int (*session_opened)(
        const struct stow_endpoint* endpoint, uint32_t session_id, stow_role_t roles, struct stow_transport* transport);

    /**
     * @brief A peer disconnected
     *
     * @param endpoint The endpoint
     * @param session_id The session that closed
     */
    void (*session_closed)(const struct stow_endpoint* endpoint, uint32_t session_id);

    /**
     * @brief One complete Stow message arrived
     *
     * @details The endpoint takes its own reference to @p buf; the caller keeps its reference. A message the endpoint
     * cannot take, for example because the session is not open, is dropped and logged.
     *
     * @param endpoint The endpoint
     * @param session_id Session the message arrived on
     * @param buf The message
     */
    void (*receive)(const struct stow_endpoint* endpoint, uint32_t session_id, struct net_buf* buf);
};

//**********************************************************
//* Functions
//**********************************************************

/**
 * @brief Check that a transport can carry Stow messages: it sends, and its fixed-size buffers outlast its headroom and
 * tailroom
 *
 * @param transport The transport, or NULL
 *
 * @return true when sessions may be bound to it
 */
static inline bool stow_transport_usable(const struct stow_transport* transport)
{
    return transport != NULL && transport->send != NULL && transport->pool != NULL &&
           transport->pool->alloc->cb == &net_buf_fixed_cb &&
           transport->headroom + transport->tailroom <= transport->pool->alloc->max_alloc_size;
}

/**
 * @brief Allocate an outgoing Stow message and find how many bytes it may hold
 *
 * @param transport The transport the message is for
 * @param limit Largest message the receiver accepts, or 0 for no limit beyond the buffer
 * @param[out] buf_out The message, with the transport's headroom reserved; write at its tail
 * @param[out] budget_out Bytes the message may use
 *
 * @return SUCCESS when the message is allocated
 * @return -EBUSY when the transport has no free buffer
 * @acquires net_buf buf_out on-success
 */
static inline int stow_transport_alloc(
    const struct stow_transport* transport, uint32_t limit, struct net_buf** buf_out, size_t* budget_out)
{
    struct net_buf* buf = net_buf_alloc(transport->pool, K_NO_WAIT);
    if (buf == NULL)
    {
        return -EBUSY;
    }
    __ASSERT(
        transport->headroom + transport->tailroom <= buf->size, "Bind only a transport stow_transport_usable accepts");
    net_buf_reserve(buf, transport->headroom);

    size_t budget = net_buf_tailroom(buf) - transport->tailroom;
    if (limit != 0U && limit < budget)
    {
        budget = limit;
    }
    *buf_out = buf;
    *budget_out = budget;
    PASS_OWNERSHIP(buf);
    return SUCCESS;
}

/**
 * @brief Send one complete Stow message, dropping it when the transport refuses it
 *
 * @param transport The transport to send on
 * @param session_id Session the message belongs to
 * @param buf The message, ownership is always taken
 *
 * @return SUCCESS when the transport took the message, otherwise the transport's error
 * @releases net_buf buf
 */
static inline int stow_transport_send(struct stow_transport* transport, uint32_t session_id, struct net_buf* buf)
{
    int ret = transport->send(transport, session_id, buf);
    if (ret != SUCCESS)
    {
        net_buf_unref(buf);
    }
    return ret;
}

/**
 * @}
 */
