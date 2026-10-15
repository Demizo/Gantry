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

#include <gantry/error.h>
#include <gantry/module.h>
#include <gantry/stow/stow_cobs_transport.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net_buf.h>
#include <zephyr/sys/ring_buffer.h>

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
static void uart_interface_init(void);
static void uart_interface_thread(void* arg1, void* arg2, void* arg3);

//**********************************************************
//* Static Variable Definitions
//**********************************************************

K_MSGQ_DEFINE(uart_tx_queue, sizeof(struct net_buf*), CONFIG_UART_INTERFACE_QUEUE_DEPTH, sizeof(void*));

static const struct device* uart_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));

// Module registration
GANTRY_MODULE_DEFINE(
    uart_interface, uart_interface_init, uart_interface_thread, CONFIG_UART_INTERFACE_STACK_SIZE,
    CONFIG_UART_INTERFACE_THREAD_PRIORITY);

// Semaphore for signalling when data has been received
K_SEM_DEFINE(rx_sem, 0, 1);

// Ring buffer for incoming data
RING_BUF_DECLARE(rx_ring_buf, CONFIG_UART_INTERFACE_RX_RING_BUF_SIZE);

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

static void uart_interface_init(void)
{
    // Initialize the UART device
    if (!device_is_ready(uart_dev))
    {
        LOG_ERR("UART device not ready");
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

    struct net_buf* frame = NULL;
    bool rx_failing = false;

    struct k_poll_event poll_events[2] = {
        K_POLL_EVENT_STATIC_INITIALIZER(K_POLL_TYPE_SEM_AVAILABLE, K_POLL_MODE_NOTIFY_ONLY, &rx_sem, 0),
        K_POLL_EVENT_STATIC_INITIALIZER(K_POLL_TYPE_MSGQ_DATA_AVAILABLE, K_POLL_MODE_NOTIFY_ONLY, &uart_tx_queue, 0),
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
                int ret = stow_cobs_transport_feed(&uart_link, &byte, 1);
                if (ret != SUCCESS && !rx_failing)
                {
                    LOG_WRN("Failed to receive UART frame: %d", ret);
                }
                rx_failing = (ret != SUCCESS);
            }

            poll_events[0].state = K_POLL_STATE_NOT_READY;
        }

        if (poll_events[1].state == K_POLL_STATE_MSGQ_DATA_AVAILABLE)
        {
            while (k_msgq_get(&uart_tx_queue, (void*)&frame, K_NO_WAIT) == 0)
            {
                // Transmit the COBS encoded message
                for (size_t i = 0; i < frame->len; i++)
                {
                    uart_poll_out(uart_dev, frame->data[i]);
                }

                net_buf_unref(frame);
            }

            poll_events[1].state = K_POLL_STATE_NOT_READY;
        }
    }
}

//**********************************************************
//* Public Function Definitions
//**********************************************************
