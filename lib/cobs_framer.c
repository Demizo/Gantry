/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief COBS framer implementation utilizing Zephyr API
 *
 * @author Demizo (demizodemazo@gmail.com)
 */

#include <gantry/cobs_framer.h>
#include <gantry/error.h>
#include <string.h>
#include <sys/errno.h>
#include <zephyr/data/cobs.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net_buf.h>

/**
 * @brief Logger for module
 */
LOG_MODULE_REGISTER(cobs_framer, CONFIG_GANTRY_COBS_FRAMER_LOG_LEVEL);

//**********************************************************
//* Static Function Declarations
//**********************************************************

static int alloc_rx_buf(struct cobs_frame_decoder* frame_decoder);
static int internal_stream_cb(const uint8_t* buf, size_t len, void* user_data);

//**********************************************************
//* Static Function Definitions
//**********************************************************

static int alloc_rx_buf(struct cobs_frame_decoder* framer)
{
    framer->rx_buf = net_buf_alloc(framer->pool, K_NO_WAIT);
    if (framer->rx_buf == NULL)
    {
        return -ENOMEM;
    }
    return SUCCESS;
}

static int internal_stream_cb(const uint8_t* buf, size_t len, void* user_data)
{
    struct cobs_frame_decoder* frame_decoder = user_data;

    // Ignore chunks if we are discarding until the next frame sync
    if (frame_decoder->is_syncing)
    {
        return 0;
    }

    //? NOTE: A completed frame is signaled with a NULL buffer and 0 length when COBS_FLAG_TRAILING_DELIMITER is used.
    if (buf == NULL && len == 0)
    {
        if (frame_decoder->rx_buf->len > 0)
        {
            frame_decoder->cb(frame_decoder->rx_buf, frame_decoder->user_data);
            frame_decoder->rx_buf = NULL;

            if (alloc_rx_buf(frame_decoder) != SUCCESS)
            {
                LOG_ERR("Failed to allocate rx net_buf after frame");
                frame_decoder->is_syncing = true;
                return -ENOMEM;
            }
        }
        return 0;
    }

    if (net_buf_tailroom(frame_decoder->rx_buf) < len)
    {
        LOG_WRN("COBS RX overflow, dropping frame");
        frame_decoder->is_syncing = true;
        net_buf_reset(frame_decoder->rx_buf);
        return -ENOSPC;
    }

    net_buf_add_mem(frame_decoder->rx_buf, buf, len);
    return 0;
}

//**********************************************************
//* Function Definitions
//**********************************************************

int cobs_frame_decoder_init(
    struct cobs_frame_decoder* frame_decoder, struct net_buf_pool* pool, cobs_frame_cb_t cb, void* user_data)
{
    if (frame_decoder == NULL || pool == NULL || cb == NULL)
    {
        return -EINVAL;
    }

    frame_decoder->pool = pool;
    frame_decoder->cb = cb;
    frame_decoder->user_data = user_data;
    frame_decoder->rx_buf = NULL;
    frame_decoder->is_syncing = false;

    cobs_decoder_init(&frame_decoder->dec, internal_stream_cb, frame_decoder, COBS_FLAG_TRAILING_DELIMITER);

    return alloc_rx_buf(frame_decoder);
}

int cobs_frame_decoder_feed(struct cobs_frame_decoder* frame_decoder, const uint8_t* data, size_t len)
{
    if (frame_decoder->rx_buf == NULL)
    {
        if (alloc_rx_buf(frame_decoder) != SUCCESS)
        {
            return -ENOMEM;
        }
        // If we missed data due to memory exhaustion, force a resync
        frame_decoder->is_syncing = true;
    }

    size_t processed = 0;

    while (processed < len)
    {
        if (frame_decoder->is_syncing)
        {
            // Scan for the next delimiter to resync
            const uint8_t* zero_ptr = memchr(data + processed, 0U, len - processed);
            if (zero_ptr != NULL)
            {
                frame_decoder->is_syncing = false;
                if (frame_decoder->rx_buf)
                {
                    net_buf_reset(frame_decoder->rx_buf);
                }

                // Reset the internal decoder
                cobs_decoder_init(&frame_decoder->dec, internal_stream_cb, frame_decoder, COBS_FLAG_TRAILING_DELIMITER);

                processed = (zero_ptr - data) + 1;  // Skip the delimiter itself
            }
            else
            {
                // All bytes consumed, still waiting for delimiter
                break;
            }
            continue;
        }

        // Pass the chunk to Zephyr's stream decoder
        int ret = cobs_decoder_write(&frame_decoder->dec, data + processed, len - processed);
        if (ret < 0)
        {
            LOG_WRN("COBS decode error, dropping frame (%d)", ret);
            frame_decoder->is_syncing = true;
            if (frame_decoder->rx_buf)
            {
                net_buf_reset(frame_decoder->rx_buf);
            }
            // Loop continues and falls into the is_syncing block to scan for the next delimiter
        }
        else
        {
            //?NOTE: On success, ret is the number of bytes from the input buffer that were used
            processed += ret;
        }
    }

    return SUCCESS;
}

int cobs_frame_encode(struct net_buf_pool* pool, const struct net_buf* input, struct net_buf** output)
{
    if (pool == NULL || input == NULL || output == NULL)
    {
        return -EINVAL;
    }

    size_t encoded_max = cobs_max_encoded_len(input->len, COBS_FLAG_TRAILING_DELIMITER);

    struct net_buf* out = net_buf_alloc(pool, K_NO_WAIT);
    if (out == NULL)
    {
        return -ENOMEM;
    }

    if (net_buf_tailroom(out) < encoded_max)
    {
        LOG_ERR("Output pool buf too small: need %zu, have %zu", encoded_max, net_buf_tailroom(out));
        net_buf_unref(out);
        return -ENOSPC;
    }

    int ret = cobs_encode((struct net_buf*)input, out, COBS_FLAG_TRAILING_DELIMITER);
    if (ret < 0)
    {
        LOG_ERR("Zephyr COBS encode failed: %d", ret);
        net_buf_unref(out);
        return ret;
    }

    *output = out;
    return SUCCESS;
}
