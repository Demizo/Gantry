/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief UART Interface
 *
 * @author Demizo (demizodemazo@gmail.com)
 *
 */

#include "uart_interface.h"

#include <gantry/cobs_framer.h>
#include <gantry/error.h>
#include <gantry/memory.h>
#include <gantry/module.h>
#include <gantry/stow/stow_protocol.h>
#include <gantry/stow/types/stow_types.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net_buf.h>
#include <zephyr/sys/ring_buffer.h>

#include "msg_event.h"
#include "session_manager.h"

/**
 * @brief Logger for module
 */
LOG_MODULE_REGISTER(uart_interface, CONFIG_UART_INTERFACE_LOG_LEVEL);

//**********************************************************
//* Local Definitions
//**********************************************************

//**********************************************************
//* Static Function Declarations
//**********************************************************

static void uart_isr(const struct device* dev, void* user_data);
static void rx_frame_cb(struct net_buf* buf, void* user_data);
static void uart_interface_init(void);
static void uart_interface_thread(void* arg1, void* arg2, void* arg3);

//**********************************************************
//* Static Variable Definitions
//**********************************************************

K_MSGQ_DEFINE(uart_interface_queue, sizeof(event_t*), CONFIG_UART_INTERFACE_QUEUE_DEPTH, sizeof(void*));

static const struct device* uart_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));

// Module registration
GANTRY_MODULE_DEFINE(
    uart_interface, uart_interface_init, uart_interface_thread, CONFIG_UART_INTERFACE_STACK_SIZE,
    CONFIG_UART_INTERFACE_THREAD_PRIORITY);

// Shared response pool from session manager
extern struct net_buf_pool* const response_pool_ptr;

// Semaphore for signalling when data has been received
K_SEM_DEFINE(rx_sem, 0, 1);

// Ring buffer for incoming data
RING_BUF_DECLARE(rx_ring_buf, CONFIG_UART_INTERFACE_RX_RING_BUF_SIZE);

// COBS decoder for incoming messages
static struct cobs_frame_decoder frame_decoder;

//**********************************************************
//* Static Function Definitions
//**********************************************************

static void uart_isr(const struct device* dev, void* user_data)
{
    ARG_UNUSED(user_data);

    uart_irq_update(dev);
    while (uart_irq_rx_ready(dev))
    {
        uint8_t byte = 0U;
        if (uart_fifo_read(dev, &byte, 1) == 1)
        {
            ring_buf_put(&rx_ring_buf, &byte, 1);
        }
    }

    // Signal that data is available
    k_sem_give(&rx_sem);
}

static void rx_frame_cb(struct net_buf* buf, void* user_data)
{
    ARG_UNUSED(user_data);

    int ret = stow_protocol_handle_rx(UART_SESSION_ID, UART_SESSION_AUTH, buf);
    if (ret != SUCCESS)
    {
        LOG_WRN("Failed to submit stow protocol message: %d", ret);
    }
    net_buf_unref(buf);
}

static void uart_interface_init(void)
{
    int ret;

    // Initialize the UART device
    if (!device_is_ready(uart_dev))
    {
        LOG_ERR("UART device not ready");
        return;
    }

    // Set up COBS decoder
    ret = cobs_frame_decoder_init(&frame_decoder, response_pool_ptr, rx_frame_cb, NULL);
    if (ret != SUCCESS)
    {
        LOG_ERR("Failed to create COBS decoder: %d", ret);
        return;
    }

    // Configure UART interrupts
    uart_irq_callback_user_data_set(uart_dev, uart_isr, NULL);
    uart_irq_rx_enable(uart_dev);

    LOG_INF("UART interface initialized");
}

static void uart_interface_thread(void* arg1, void* arg2, void* arg3)
{
    ARG_UNUSED(arg1);
    ARG_UNUSED(arg2);
    ARG_UNUSED(arg3);

    event_t* event = NULL;

    struct k_poll_event poll_events[2] = {
        K_POLL_EVENT_STATIC_INITIALIZER(K_POLL_TYPE_SEM_AVAILABLE, K_POLL_MODE_NOTIFY_ONLY, &rx_sem, 0),
        K_POLL_EVENT_STATIC_INITIALIZER(
            K_POLL_TYPE_MSGQ_DATA_AVAILABLE, K_POLL_MODE_NOTIFY_ONLY, &uart_interface_queue, 0),
    };

    LOG_INF("UART interface thread started");

    while (true)
    {
        k_poll(poll_events, ARRAY_SIZE(poll_events), K_FOREVER);

        // RX bytes received
        if (poll_events[0].state == K_POLL_STATE_SEM_AVAILABLE)
        {
            k_sem_take(&rx_sem, K_NO_WAIT);

            // Feed received bytes to the COBS decoder
            uint8_t byte;
            while (ring_buf_get(&rx_ring_buf, &byte, 1) == 1)
            {
                cobs_frame_decoder_feed(&frame_decoder, &byte, 1);
            }

            poll_events[0].state = K_POLL_STATE_NOT_READY;
        }

        // Event received
        if (poll_events[1].state == K_POLL_STATE_MSGQ_DATA_AVAILABLE)
        {
            while (k_msgq_get(&uart_interface_queue, (void*)&event, K_NO_WAIT) == 0)
            {
                ASSERT(event->type == &msg_event, "Unexpected event type");

                struct msg_event_payload* payload = (struct msg_event_payload*)event->data.buf;

                struct net_buf* encoded = NULL;
                int ret = cobs_frame_encode(response_pool_ptr, payload->buf, &encoded);
                if (ret == SUCCESS)
                {
                    // Transmit the COBS encoded message
                    for (size_t i = 0; i < encoded->len; i++)
                    {
                        uart_poll_out(uart_dev, encoded->data[i]);
                    }
                    net_buf_unref(encoded);
                }
                else
                {
                    LOG_ERR("COBS encode failed: %d", ret);
                }

                EVENT_UNREF(&event);
            }

            poll_events[1].state = K_POLL_STATE_NOT_READY;
        }
    }
}

//**********************************************************
//* Public Function Definitions
//**********************************************************

void uart_interface_send(struct net_buf* buf)
{
    event_t* event = NULL;
    int ret = msg_event_alloc(MSG_MEDIUM_UART, MSG_DIRECTION_TX, buf, &event);
    if (ret != SUCCESS)
    {
        LOG_ERR("Failed to allocate outgoing message: %d", ret);
        net_buf_unref(buf);
        NOT_REFERENCED(event);
        return;
    }

    // Pass the message event to the thread
    ret = k_msgq_put(&uart_interface_queue, (const void*)&event, K_NO_WAIT);
    if (ret != SUCCESS)
    {
        LOG_WRN("Failed to queue message event: %d", ret);
        EVENT_UNREF(&event);
    }

    // Freed when processed by the thread
    PASS_OWNERSHIP(event);
}
