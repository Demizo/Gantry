/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Session Manager
 *
 * @author Demizo (demizodemazo@gmail.com)
 *
 */

#include "session_manager.h"

#include <gantry/cobs_framer.h>
#include <gantry/error.h>
#include <gantry/module.h>
#include <gantry/stow/stow_cobs_transport.h>
#include <gantry/stow/stow_protocol.h>
#include <generated_stow_items.h>
#include <zephyr/logging/log.h>
#include <zephyr/net_buf.h>

#include "ble_interface.h"
#include "uart_interface.h"

/**
 * @brief Logger for module
 */
LOG_MODULE_REGISTER(session_manager, CONFIG_SESSION_MANAGER_LOG_LEVEL);

//**********************************************************
//* Local Definitions
//**********************************************************

#define MESSAGE_BUF_COUNT 10U
#define FRAME_BUF_SIZE COBS_ENCODE_MAX_SIZE(CONFIG_SESSION_MANAGER_MAX_MESSAGE_SIZE)
#define FRAME_BUF_COUNT 4U

//**********************************************************
//* Static Function Declarations
//**********************************************************

static int queue_frame(struct net_buf* frame, void* user_data);
static void session_manager_init(void);

//**********************************************************
//* Static Variable Definitions
//**********************************************************

NET_BUF_POOL_DEFINE(message_pool, MESSAGE_BUF_COUNT, CONFIG_SESSION_MANAGER_MAX_MESSAGE_SIZE, 0, NULL);
NET_BUF_POOL_DEFINE(uart_frame_pool, FRAME_BUF_COUNT, FRAME_BUF_SIZE, 0, NULL);
NET_BUF_POOL_DEFINE(ble_frame_pool, FRAME_BUF_COUNT, FRAME_BUF_SIZE, 0, NULL);

struct stow_cobs_transport uart_link;
struct stow_cobs_transport ble_link;

// Module registration
GANTRY_LIBRARY_MODULE_DEFINE(session_manager, session_manager_init);

//**********************************************************
//* Static Function Definitions
//**********************************************************

/**
 * @brief Queue an encoded frame for an interface's thread to send
 *
 * @details Matches @ref stow_cobs_write_t.
 *
 * @param frame Encoded frame
 * @param user_data The interface's transmit queue
 *
 * @return SUCCESS when the frame was queued
 * @return -ENOBUFS when the queue is full
 */
static int queue_frame(struct net_buf* frame, void* user_data)
{
    return (k_msgq_put((struct k_msgq*)user_data, (const void*)&frame, K_NO_WAIT) == 0) ? SUCCESS : -ENOBUFS;
}

static void session_manager_init(void)
{
    int ret = stow_protocol_init();
    ASSERT(ret == SUCCESS, "Failed to initialize the stow protocol");

    const struct stow_cobs_transport_config uart_config = {
        .endpoint = &stow_protocol_endpoint,
        .session_id = UART_SESSION_ID,
        .roles = UART_SESSION_AUTH,
        .message_pool = &message_pool,
        .frame_pool = &uart_frame_pool,
        .write = queue_frame,
        .user_data = &uart_tx_queue,
    };
    ret = stow_cobs_transport_init(&uart_link, &uart_config);
    ASSERT(ret == SUCCESS, "Failed to initialize the UART transport");

    const struct stow_cobs_transport_config ble_config = {
        .endpoint = &stow_protocol_endpoint,
        .session_id = BLE_SESSION_ID,
        .roles = BLE_SESSION_AUTH,
        .message_pool = &message_pool,
        .frame_pool = &ble_frame_pool,
        .write = queue_frame,
        .user_data = &ble_tx_queue,
    };
    ret = stow_cobs_transport_init(&ble_link, &ble_config);
    ASSERT(ret == SUCCESS, "Failed to initialize the BLE transport");

    // The UART session is always open
    ret = stow_cobs_transport_open(&uart_link);
    if (ret != SUCCESS)
    {
        LOG_ERR("Failed to open UART session: %d", ret);
    }
}

//**********************************************************
//* Public Function Definitions
//**********************************************************
