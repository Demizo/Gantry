/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief The UART link to the peer, carrying Stow in both directions over the raw COBS transport
 *
 * @author Demizo (demizodemazo@gmail.com)
 */

#include <gantry/error.h>
#include <gantry/module.h>
#include <gantry/stow/stow_cobs_transport.h>
#include <gantry/stow/stow_protocol.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net_buf.h>
#include <zephyr/sys/ring_buffer.h>

#include "peer.h"

LOG_MODULE_REGISTER(peer_link, LOG_LEVEL_INF);

//**********************************************************
//* Local Definitions
//**********************************************************

#define MESSAGE_SIZE 512U
#define MESSAGE_COUNT 8U
#define FRAME_COUNT 8U
#define SESSION_ID 1U
#define RX_RING_SIZE 1024U
#define STACK_SIZE 2048U
#define PRIORITY 6

//**********************************************************
//* Static Function Declarations
//**********************************************************

static int queue_frame(struct net_buf* frame, void* user_data);
static void uart_isr(const struct device* dev, void* user_data);
static void peer_link_init(void);
static void peer_link_thread(void* arg1, void* arg2, void* arg3);

//**********************************************************
//* Static Variable Definitions
//**********************************************************

NET_BUF_POOL_DEFINE(message_pool, MESSAGE_COUNT, MESSAGE_SIZE, 0, NULL);
NET_BUF_POOL_DEFINE(frame_pool, FRAME_COUNT, COBS_ENCODE_MAX_SIZE(MESSAGE_SIZE), 0, NULL);
K_MSGQ_DEFINE(tx_queue, sizeof(struct net_buf*), FRAME_COUNT, sizeof(void*));
K_SEM_DEFINE(rx_sem, 0, 1);
RING_BUF_DECLARE(rx_ring, RX_RING_SIZE);

static const struct device* uart_dev = DEVICE_DT_GET(DT_CHOSEN(gantry_peer_uart));
static struct stow_cobs_transport link;

GANTRY_MODULE_DEFINE(peer_link, peer_link_init, peer_link_thread, STACK_SIZE, PRIORITY);

//**********************************************************
//* Static Function Definitions
//**********************************************************

/**
 * @brief Queue an encoded frame for the link thread to send
 *
 * @details Matches @ref stow_cobs_write_t.
 *
 * @param frame Encoded frame
 * @param user_data Unused
 *
 * @return SUCCESS when the frame was queued
 * @return -ENOBUFS when the queue is full
 */
static int queue_frame(struct net_buf* frame, void* user_data)
{
    ARG_UNUSED(user_data);
    return (k_msgq_put(&tx_queue, (const void*)&frame, K_NO_WAIT) == 0) ? SUCCESS : -ENOBUFS;
}

static void uart_isr(const struct device* dev, void* user_data)
{
    ARG_UNUSED(user_data);

    uint8_t data[64];
    int len;
    uart_irq_update(dev);
    while (uart_irq_rx_ready(dev) && (len = uart_fifo_read(dev, data, sizeof(data))) > 0)
    {
        if (ring_buf_put(&rx_ring, data, len) < (uint32_t)len)
        {
            LOG_WRN("Peer UART receive overflow");
        }
    }
    k_sem_give(&rx_sem);
}

static void peer_link_init(void)
{
    int ret = stow_protocol_init();
    ASSERT(ret == SUCCESS, "Failed to initialize the Stow protocol");

    const struct stow_cobs_transport_config config = {
        .server = &stow_protocol_endpoint,
        .session_id = SESSION_ID,
        .roles = STOW_ROLE_ANY,
        .message_pool = &message_pool,
        .frame_pool = &frame_pool,
        .write = queue_frame,
        .user_data = NULL,
    };
    ret = stow_cobs_transport_init(&link, &config);
    ASSERT(ret == SUCCESS, "Failed to initialize the peer link");

    ASSERT(device_is_ready(uart_dev), "Peer UART not ready");
    uart_irq_callback_user_data_set(uart_dev, uart_isr, NULL);
    uart_irq_rx_enable(uart_dev);
}

static void peer_link_thread(void* arg1, void* arg2, void* arg3)
{
    ARG_UNUSED(arg1);
    ARG_UNUSED(arg2);
    ARG_UNUSED(arg3);

    struct k_poll_event events[2] = {
        K_POLL_EVENT_STATIC_INITIALIZER(K_POLL_TYPE_SEM_AVAILABLE, K_POLL_MODE_NOTIFY_ONLY, &rx_sem, 0),
        K_POLL_EVENT_STATIC_INITIALIZER(K_POLL_TYPE_MSGQ_DATA_AVAILABLE, K_POLL_MODE_NOTIFY_ONLY, &tx_queue, 0),
    };

    while (true)
    {
        k_poll(events, ARRAY_SIZE(events), K_FOREVER);

        if (events[0].state == K_POLL_STATE_SEM_AVAILABLE)
        {
            k_sem_take(&rx_sem, K_NO_WAIT);
            uint8_t data[64];
            uint32_t len;
            while ((len = ring_buf_get(&rx_ring, data, sizeof(data))) > 0U)
            {
                int ret = stow_cobs_transport_feed(&link, data, len);
                if (ret != SUCCESS)
                {
                    LOG_WRN("Dropped received bytes: %d", ret);
                }
            }
            events[0].state = K_POLL_STATE_NOT_READY;
        }

        if (events[1].state == K_POLL_STATE_MSGQ_DATA_AVAILABLE)
        {
            struct net_buf* frame = NULL;
            while (k_msgq_get(&tx_queue, (void*)&frame, K_NO_WAIT) == 0)
            {
                for (size_t i = 0; i < frame->len; i++)
                {
                    uart_poll_out(uart_dev, frame->data[i]);
                }
                net_buf_unref(frame);
            }
            events[1].state = K_POLL_STATE_NOT_READY;
        }
    }
}

//**********************************************************
//* Public Function Definitions
//**********************************************************

int peer_link_up(void) { return stow_cobs_transport_open(&link); }

int peer_link_connect(void) { return stow_cobs_transport_open_client(&link, &peer_client.endpoint); }

void peer_link_down(void) { stow_cobs_transport_close(&link); }
