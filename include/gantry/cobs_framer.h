/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief COBS framer.
 *
 * @details Provides and encoder and decoder for COBS frames. The decoder accumulates decoded bytes directly into a
 * net_buf allocated from the caller-supplied pool. Complete frames are returned the caller via a callback. The encoder
 * allocates an output net_buf and performs a single encoding pass.
 *
 * @author Demizo (demizodemazo@gmail.com)
 */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <zephyr/data/cobs.h>
#include <zephyr/net_buf.h>

/**
 * @addtogroup cobs_framer Cobs Framer
 * @{
 */

//**********************************************************
//* Definitions
//**********************************************************

/**
 * @brief Helper for calculating the worst-case size of data encoded in a CBOR frame
 */
#define COBS_ENCODE_MAX_SIZE(src_len) ((src_len) + ((src_len) / 254U) + 2U)

//**********************************************************
//* Typedefs, Enums, and Structs
//**********************************************************

/**
 * @brief Callback invoked when a complete COBS frame has been decoded.
 *
 * @details The callback receives ownership of @p buf. The consumer must call
 * `net_buf_unref(buf)` when done with it.
 *
 * @param buf Decoded payload
 * @param user_data Opaque pointer supplied to cobs_frame_decoder_init
 */
typedef void (*cobs_frame_cb_t)(struct net_buf* buf, void* user_data);

/**
 * @brief State for a COBS frame decoder instance.
 *
 * @details Initialise with cobs_frame_decoder_init before use.
 */
struct cobs_frame_decoder
{
    struct net_buf_pool* pool; /**< Pool from which net_bufs are allocated */
    struct net_buf* rx_buf;    /**< net_buf accumulating the current decoded frame */
    cobs_frame_cb_t cb;        /**< Callback fired with complete frames */
    void* user_data;           /**< Opaque value forwarded to the callback */
    struct cobs_decoder dec;   /**< Internal COBS decoder instance */
    bool is_syncing;           /**< Whether the decoder is awaiting a sync byte */
};

//**********************************************************
//* Functions
//**********************************************************

/**
 * @brief Initialise a COBS frame decoder instance.
 *
 * @details Allocates the first receive net_buf from @p pool. Must be called
 * before cobs_frame_decoder_feed.
 *
 * @param frame_decoder Frame decoder instance to initialise
 * @param pool net_buf pool used for decoded frame buffers
 * @param cb Called when the decoder receives a complete COBS frame
 * @param user_data User data forwarded to the callback
 *
 * @return 0 on success
 * @return -EINVAL if any pointer argument is NULL
 * @return -ENOMEM if the initial net_buf cannot be allocated from @p pool
 */
int cobs_frame_decoder_init(
    struct cobs_frame_decoder* frame_decoder, struct net_buf_pool* pool, cobs_frame_cb_t cb, void* user_data);

/**
 * @brief Feed bytes into the COBS frame decoder.
 *
 * @details The frame decoder callback is called when a complete frame is received. Frames that overflow the pool buffer
 * size are dropped.
 *
 * @param frame_decoder Frame decoder instance
 * @param data Byte(s) to process
 * @param len Number of bytes in @p data
 *
 * @return 0 on success
 * @return -ENOMEM if a replacement net_buf cannot be allocated after a complete frame; the framer is unable to receive
 * further frames until a buffer becomes available
 */
int cobs_frame_decoder_feed(struct cobs_frame_decoder* frame_decoder, const uint8_t* data, size_t len);

/**
 * @brief COBS-encode a net_buf payload into a newly allocated net_buf.
 *
 * @details Allocates an output net_buf from @p pool, performs a single
 * encoding pass, and returns it via @p output.
 *
 * The @p input net_buf is not modified and its reference count is not changed.
 * The caller owns @p *output and must call `net_buf_unref` after transmission.
 *
 * @param pool Pool from which the encoded output net_buf is allocated
 * @param input net_buf containing the payload to encode
 * @param output a net_buf containing the encoded output
 *
 * @return 0 on success
 * @return -EINVAL if any pointer argument is NULL
 * @return -ENOMEM if the output net_buf cannot be allocated from @p pool
 * @return -ENOSPC if the allocated net_buf is too small for the encoded output
 */
int cobs_frame_encode(struct net_buf_pool* pool, const struct net_buf* input, struct net_buf** output);

/**
 * @}
 */
