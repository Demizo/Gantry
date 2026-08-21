/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief BLE Interface
 *
 * @author Demizo (demizodemazo@gmail.com)
 *
 */

#include "ble_interface.h"

#include <gantry/cobs_framer.h>
#include <gantry/error.h>
#include <gantry/memory.h>
#include <gantry/module.h>
#include <gantry/stow/stow_protocol.h>
#include <generated_stow_items.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net_buf.h>

#include "gantry/event.h"
#include "msg_event.h"
#include "session_manager.h"
#include "zephyr/sys/clock.h"

/**
 * @brief Logger for module
 */
LOG_MODULE_REGISTER(ble_interface, CONFIG_BLE_INTERFACE_LOG_LEVEL);

//**********************************************************
//* Local Definitions
//**********************************************************

//**********************************************************
//* Static Function Declarations
//**********************************************************

static void on_connected(struct bt_conn* conn, uint8_t err);
static void on_disconnected(struct bt_conn* conn, uint8_t reason);
static ssize_t nus_rx_write(
    struct bt_conn* conn, const struct bt_gatt_attr* attr, const void* data, uint16_t len, uint16_t offset,
    uint8_t flags);
static void nus_tx_ccc_changed(const struct bt_gatt_attr* attr, uint16_t value);
static int ble_interface_tx(const uint8_t* data, uint16_t len);
static void rx_frame_cb(struct net_buf* buf, void* user_data);
static void ble_interface_init(void);
static void ble_interface_thread(void* arg1, void* arg2, void* arg3);

//**********************************************************
//* Static Variable Definitions
//**********************************************************

K_MSGQ_DEFINE(ble_interface_queue, sizeof(event_t*), CONFIG_BLE_INTERFACE_QUEUE_DEPTH, sizeof(void*));

// Module registration
GANTRY_MODULE_DEFINE(
    ble_interface, ble_interface_init, ble_interface_thread, CONFIG_BLE_INTERFACE_STACK_SIZE,
    CONFIG_BLE_INTERFACE_THREAD_PRIORITY);

// Shared response pool from session manager
extern struct net_buf_pool* const response_pool_ptr;

// Pool for received data
NET_BUF_POOL_DEFINE(ble_rx_pool, CONFIG_BLE_INTERFACE_RX_POOL_COUNT, CONFIG_BLE_INTERFACE_RX_POOL_BUF_SIZE, 0, NULL);

// The current BLE connection
static struct bt_conn* current_conn;

// COBS decoder for incoming messages
static struct cobs_frame_decoder frame_decoder;

// Callbacks for tracking BLE connection state
BT_CONN_CB_DEFINE(ble_interface_conn_cb) = {
    .connected = on_connected,
    .disconnected = on_disconnected,
};

// Nordic UART service
BT_GATT_SERVICE_DEFINE(
    nus_svc, BT_GATT_PRIMARY_SERVICE(BT_UUID_NUS),
    BT_GATT_CHARACTERISTIC(BT_UUID_NUS_TX, BT_GATT_CHRC_NOTIFY, BT_GATT_PERM_NONE, NULL, NULL, NULL),
    BT_GATT_CCC(nus_tx_ccc_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
    BT_GATT_CHARACTERISTIC(
        BT_UUID_NUS_RX, BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP, BT_GATT_PERM_WRITE, NULL, nus_rx_write,
        NULL));

//**********************************************************
//* Static Function Definitions
//**********************************************************

static void on_connected(struct bt_conn* conn, uint8_t err)
{
    if (err != 0)
    {
        return;
    }
    current_conn = bt_conn_ref(conn);
}

static void on_disconnected(struct bt_conn* conn, uint8_t reason)
{
    ARG_UNUSED(conn);
    ARG_UNUSED(reason);
    if (current_conn != NULL)
    {
        bt_conn_unref(current_conn);
        current_conn = NULL;
    }
}

static ssize_t nus_rx_write(
    struct bt_conn* conn, const struct bt_gatt_attr* attr, const void* data, uint16_t len, uint16_t offset,
    uint8_t flags)
{
    ARG_UNUSED(conn);
    ARG_UNUSED(attr);
    ARG_UNUSED(offset);
    ARG_UNUSED(flags);

    struct net_buf* buf = net_buf_alloc(&ble_rx_pool, K_NO_WAIT);
    if (buf == NULL)
    {
        LOG_WRN("BLE RX pool exhausted; dropping %u bytes", len);
        return len;
    }

    if (net_buf_tailroom(buf) < len)
    {
        LOG_WRN("BLE RX chunk (%u) exceeds buf size; dropping", len);
        net_buf_unref(buf);
        return len;
    }

    net_buf_add_mem(buf, data, len);

    event_t* event = NULL;
    int ret = msg_event_alloc(MSG_MEDIUM_BLE, MSG_DIRECTION_RX, buf, &event);
    if (ret != SUCCESS)
    {
        LOG_WRN("Failed to allocate rx event: %d", ret);
        net_buf_unref(buf);
        NOT_REFERENCED(event);
        return len;
    }

    if (k_msgq_put(&ble_interface_queue, (const void*)&event, K_NO_WAIT) != 0)
    {
        LOG_WRN("Failed to queue message event; dropping %u bytes", len);
        event_unref(&event);
    }

    PASS_OWNERSHIP(event);
    return len;
}

static void nus_tx_ccc_changed(const struct bt_gatt_attr* attr, uint16_t value)
{
    ARG_UNUSED(attr);
    LOG_DBG("NUS TX notifications %s", value == BT_GATT_CCC_NOTIFY ? "enabled" : "disabled");
}

static int ble_interface_tx(const uint8_t* data, uint16_t len)
{
    if (current_conn == NULL)
    {
        return -ENOTCONN;
    }
    if (data == NULL || len == 0U)
    {
        return -EINVAL;
    }

    uint16_t mtu = bt_gatt_get_mtu(current_conn);
    if (mtu <= 3U)
    {
        return -EIO;
    }
    uint16_t chunk_size = mtu - 3U;  // ATT notify header

    const struct bt_gatt_attr* tx_attr = &nus_svc.attrs[1];

    uint16_t offset = 0U;
    while (offset < len)
    {
        uint16_t remaining = len - offset;
        uint16_t chunk = remaining < chunk_size ? remaining : chunk_size;

        int ret = bt_gatt_notify(current_conn, tx_attr, &data[offset], chunk);
        if (ret != 0)
        {
            LOG_WRN("bt_gatt_notify failed at offset %u: %d", offset, ret);
            return ret;
        }
        offset += chunk;
    }

    return SUCCESS;
}

static void rx_frame_cb(struct net_buf* buf, void* user_data)
{
    ARG_UNUSED(user_data);

    int ret = stow_protocol_handle_rx(BLE_SESSION_ID, BLE_SESSION_AUTH, buf);
    if (ret != SUCCESS)
    {
        LOG_WRN("Failed to submit stow protocol message: %d", ret);
    }
    net_buf_unref(buf);
}

static void ble_interface_init(void)
{
    // Set up COBS decoder
    int ret = cobs_frame_decoder_init(&frame_decoder, response_pool_ptr, rx_frame_cb, NULL);
    if (ret != SUCCESS)
    {
        LOG_ERR("Failed to create COBS decoder: %d", ret);
        return;
    }

    LOG_INF("BLE interface initialized");
}

static void ble_interface_thread(void* arg1, void* arg2, void* arg3)
{
    ARG_UNUSED(arg1);
    ARG_UNUSED(arg2);
    ARG_UNUSED(arg3);

    int ret;
    k_timepoint_t timeout_timepoint = sys_timepoint_calc(K_FOREVER);
    event_t* event = NULL;

    LOG_INF("BLE interface thread started");

    while (true)
    {
        if (k_msgq_get(&ble_interface_queue, (void*)&event, sys_timepoint_timeout(timeout_timepoint)) == 0)
        {
            ASSERT(event->type == &msg_event, "Unexpected event type");

            struct msg_event_payload* payload = (struct msg_event_payload*)event->data.buf;

            switch (payload->direction)
            {
                case MSG_DIRECTION_RX:
                    cobs_frame_decoder_feed(&frame_decoder, payload->buf->data, payload->buf->len);
                    break;

                case MSG_DIRECTION_TX:
                {
                    struct net_buf* encoded = NULL;
                    ret = cobs_frame_encode(response_pool_ptr, payload->buf, &encoded);
                    if (ret == SUCCESS)
                    {
                        ret = ble_interface_tx(encoded->data, encoded->len);
                        if (ret != SUCCESS && ret != -ENOTCONN)
                        {
                            LOG_WRN("Failed to transmit over BLE: %d", ret);
                        }
                        net_buf_unref(encoded);
                    }
                    else
                    {
                        LOG_ERR("COBS encode failed: %d", ret);
                    }
                    break;
                }
            }

            EVENT_UNREF(&event);
        }
        else
        {
            // Timed out, nothing to do
        }
    }
}

//**********************************************************
//* Public Function Definitions
//**********************************************************

void ble_interface_send(struct net_buf* buf)
{
    event_t* event = NULL;
    int ret = msg_event_alloc(MSG_MEDIUM_BLE, MSG_DIRECTION_TX, buf, &event);
    if (ret != SUCCESS)
    {
        LOG_ERR("Failed to allocate outgoing message: %d", ret);
        net_buf_unref(buf);
        NOT_REFERENCED(event);
        return;
    }

    // Pass the message event to the thread
    ret = k_msgq_put(&ble_interface_queue, (const void*)&event, K_NO_WAIT);
    if (ret != SUCCESS)
    {
        LOG_WRN("Failed to queue outgoing message event");
        EVENT_UNREF(&event);
    }

    // Freed when processed by the thread
    PASS_OWNERSHIP(event);
}
