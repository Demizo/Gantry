/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Raw COBS Stow transport
 *
 * @author Demizo (demizodemazo@gmail.com)
 *
 */

#include <gantry/cobs_framer.h>
#include <gantry/error.h>
#include <gantry/stow/stow_cobs_transport.h>
#include <gantry/stow/stow_transport.h>
#include <sys/errno.h>
#include <zephyr/net_buf.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>

//**********************************************************
//* Static Function Declarations
//**********************************************************

static int cobs_send(struct stow_transport* transport, uint32_t session_id, struct net_buf* buf);
static void on_frame(struct net_buf* buf, void* user_data);

//**********************************************************
//* Static Function Definitions
//**********************************************************

static int cobs_send(struct stow_transport* transport, uint32_t session_id, struct net_buf* buf)
{
    ARG_UNUSED(session_id);
    struct stow_cobs_transport* cobs = CONTAINER_OF(transport, struct stow_cobs_transport, transport);

    struct net_buf* frame = NULL;
    int ret = cobs_frame_encode(cobs->config.frame_pool, buf, &frame);
    if (ret != SUCCESS)
    {
        return ret;
    }

    ret = cobs->config.write(frame, cobs->config.user_data);
    if (ret != SUCCESS)
    {
        net_buf_unref(frame);
        return ret;
    }

    net_buf_unref(buf);
    return SUCCESS;
}

static void on_frame(struct net_buf* buf, void* user_data)
{
    struct stow_cobs_transport* cobs = user_data;
    const struct stow_endpoint* endpoint = cobs->config.endpoint;

    endpoint->receive(endpoint, cobs->config.session_id, buf);
    net_buf_unref(buf);
}

//**********************************************************
//* Public Function Definitions
//**********************************************************

int stow_cobs_transport_init(struct stow_cobs_transport* cobs, const struct stow_cobs_transport_config* config)
{
    if (cobs == NULL || config == NULL || config->endpoint == NULL || config->message_pool == NULL ||
        config->frame_pool == NULL || config->write == NULL)
    {
        return -EINVAL;
    }

    cobs->config = *config;
    cobs->transport = (struct stow_transport){
        .send = cobs_send,
        .pool = config->message_pool,
        .headroom = 0U,
        .tailroom = 0U,
    };
    atomic_clear(&cobs->reset_pending);

    return cobs_frame_decoder_init(&cobs->decoder, config->message_pool, on_frame, cobs);
}

int stow_cobs_transport_open(struct stow_cobs_transport* cobs)
{
    atomic_set(&cobs->reset_pending, 1);

    const struct stow_endpoint* endpoint = cobs->config.endpoint;
    return endpoint->session_opened(endpoint, cobs->config.session_id, cobs->config.roles, &cobs->transport);
}

void stow_cobs_transport_close(struct stow_cobs_transport* cobs)
{
    atomic_set(&cobs->reset_pending, 1);

    const struct stow_endpoint* endpoint = cobs->config.endpoint;
    endpoint->session_closed(endpoint, cobs->config.session_id);
}

int stow_cobs_transport_feed(struct stow_cobs_transport* cobs, const uint8_t* data, size_t len)
{
    if (atomic_cas(&cobs->reset_pending, 1, 0))
    {
        cobs_frame_decoder_reset(&cobs->decoder);
    }
    return cobs_frame_decoder_feed(&cobs->decoder, data, len);
}
