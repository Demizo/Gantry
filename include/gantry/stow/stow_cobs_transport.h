/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Raw COBS Stow transport
 *
 * @details Carries one Stow message per COBS frame over a byte stream, one session per link. Interim, until Halyard.
 *
 * @author Demizo (demizodemazo@gmail.com)
 *
 */

#pragma once

#include <gantry/cobs_framer.h>
#include <gantry/stow/stow_transport.h>
#include <gantry/stow/types/stow_types.h>
#include <stddef.h>
#include <stdint.h>
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
 * @brief Write one encoded frame to the link
 *
 * @details On success the link takes ownership of @p frame. Otherwise ownership stays with the caller and Stow drops
 * the message. Must not block.
 *
 * @param frame COBS-encoded frame, including the trailing delimiter
 * @param user_data Opaque pointer from @ref stow_cobs_transport_config
 *
 * @return SUCCESS when the frame was accepted, otherwise a negative errno
 */
typedef int (*stow_cobs_write_t)(struct net_buf* frame, void* user_data);

/**
 * @brief Configuration for @ref stow_cobs_transport_init
 */
struct stow_cobs_transport_config
{
    const struct stow_endpoint* endpoint; /**< Stow endpoint that receives the link's messages */
    uint32_t session_id;                  /**< Session ID used for the link */
    stow_role_t roles;                    /**< Roles granted to the peer on the link */
    struct net_buf_pool* message_pool;    /**< Fixed-size pool for decoded incoming and outgoing Stow messages */
    struct net_buf_pool* frame_pool;      /**< Pool for encoded outgoing frames, sized for COBS overhead */
    stow_cobs_write_t write;              /**< Writes one encoded frame to the link */
    void* user_data;                      /**< Opaque pointer passed to @ref stow_cobs_transport_config.write */
};

/**
 * @brief Raw COBS transport instance
 */
struct stow_cobs_transport
{
    struct stow_transport transport;          /**< Contract instance handed to the endpoint */
    struct stow_cobs_transport_config config; /**< Instance configuration */
    struct cobs_frame_decoder decoder;        /**< Decoder for incoming frames */
    atomic_t reset_pending;                   /**< Whether the next feed must discard any partial frame first */
};

//**********************************************************
//* Functions
//**********************************************************

/**
 * @brief Initialize a raw COBS transport
 *
 * @param cobs Transport instance
 * @param config Instance configuration, copied
 *
 * @return SUCCESS when the transport is ready
 * @return -EINVAL when the configuration is incomplete
 * @return -ENOMEM when the first receive buffer cannot be allocated
 */
int stow_cobs_transport_init(struct stow_cobs_transport* cobs, const struct stow_cobs_transport_config* config);

/**
 * @brief Open the link's session
 *
 * @details Any partial frame left from an earlier connection is discarded by the next feed.
 *
 * @param cobs Transport instance
 *
 * @return The endpoint's result
 */
int stow_cobs_transport_open(struct stow_cobs_transport* cobs);

/**
 * @brief Close the link's session
 *
 * @details Any partial frame received on the link is discarded by the next feed.
 *
 * @param cobs Transport instance
 */
void stow_cobs_transport_close(struct stow_cobs_transport* cobs);

/**
 * @brief Feed bytes received from the link
 *
 * @details Each complete frame is handed to the endpoint as one message. Call from one thread at a time.
 *
 * @param cobs Transport instance
 * @param data Received bytes
 * @param len Number of bytes in @p data
 *
 * @return SUCCESS when the bytes were consumed
 * @return -ENOMEM when no receive buffer is available
 */
int stow_cobs_transport_feed(struct stow_cobs_transport* cobs, const uint8_t* data, size_t len);

/**
 * @}
 */
