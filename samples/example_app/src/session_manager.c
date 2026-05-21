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
#include <gantry/stow/stow_protocol.h>
#include <sys/errno.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/slist.h>

#include "ble_interface.h"
#include "uart_interface.h"

/**
 * @brief Logger for module
 */
LOG_MODULE_REGISTER(session_manager, CONFIG_SESSION_MANAGER_LOG_LEVEL);

//**********************************************************
//* Local Definitions
//**********************************************************

#define RESPONSE_BUF_SIZE COBS_ENCODE_MAX_SIZE(CONFIG_SESSION_MANAGER_MAX_RESPONSE_SIZE)
#define RESPONSE_BUF_COUNT 10U

//**********************************************************
//* Static Function Declarations
//**********************************************************

void route_response(uint32_t session_id, struct net_buf* buf);

//**********************************************************
//* Static Variable Definitions
//**********************************************************

NET_BUF_POOL_DEFINE(response_pool, RESPONSE_BUF_COUNT, RESPONSE_BUF_SIZE, 0, NULL);

struct net_buf_pool* const response_pool_ptr = &response_pool;

//**********************************************************
//* Static Function Definitions
//**********************************************************

void route_response(uint32_t session_id, struct net_buf* buf)
{
    switch (session_id)
    {
        case UART_SESSION_ID:
            uart_interface_send(buf);
            break;
        case BLE_SESSION_ID:
            ble_interface_send(buf);
            break;
        default:
            LOG_WRN("Response for unknown session: %u", session_id);
            net_buf_unref(buf);
            break;
    }
}

//**********************************************************
//* Public Function Definitions
//**********************************************************

void session_manager_init(void)
{
    const struct stow_protocol_config protocol_cfg = {
        .response_cb = route_response,
        .response_pool = response_pool_ptr,
        .headroom = 0U,
        .tailroom = 0U,
    };

    int ret = stow_protocol_init(&protocol_cfg);
    ASSERT(ret == SUCCESS, "Failed to initialize the stow protocol");
}
