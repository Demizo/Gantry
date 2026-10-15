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

#include <gantry/error.h>
#include <gantry/memory.h>
#include <gantry/module.h>
#include <gantry/stow/stow_cobs_transport.h>
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
static int ble_notify(struct bt_conn* conn, const uint8_t* data, uint16_t len);
static void ble_interface_init(void);
static void ble_interface_thread(void* arg1, void* arg2, void* arg3);

//**********************************************************
//* Static Variable Definitions
//**********************************************************

K_MSGQ_DEFINE(ble_interface_queue, sizeof(event_t*), CONFIG_BLE_INTERFACE_QUEUE_DEPTH, sizeof(void*));
K_MSGQ_DEFINE(ble_tx_queue, sizeof(struct net_buf*), CONFIG_BLE_INTERFACE_QUEUE_DEPTH, sizeof(void*));

// Module registration
GANTRY_MODULE_DEFINE(
    ble_interface, ble_interface_init, ble_interface_thread, CONFIG_BLE_INTERFACE_STACK_SIZE,
    CONFIG_BLE_INTERFACE_THREAD_PRIORITY);

// Pool for received data
NET_BUF_POOL_DEFINE(ble_rx_pool, CONFIG_BLE_INTERFACE_RX_POOL_COUNT, CONFIG_BLE_INTERFACE_RX_POOL_BUF_SIZE, 0, NULL);

// The current BLE connection
static struct bt_conn* current_conn;

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
    struct bt_conn* ref = bt_conn_ref(conn);
    uint32_t key = irq_lock();
    current_conn = ref;
    irq_unlock(key);
}

static void on_disconnected(struct bt_conn* conn, uint8_t reason)
{
    ARG_UNUSED(conn);
    ARG_UNUSED(reason);
    uint32_t key = irq_lock();
    struct bt_conn* old = current_conn;
    current_conn = NULL;
    irq_unlock(key);
    if (old != NULL)
    {
        bt_conn_unref(old);
    }

    event_t* event = NULL;
    while (k_msgq_get(&ble_interface_queue, (void*)&event, K_NO_WAIT) == 0)
    {
        EVENT_UNREF(&event);
    }

    struct net_buf* frame = NULL;
    while (k_msgq_get(&ble_tx_queue, (void*)&frame, K_NO_WAIT) == 0)
    {
        net_buf_unref(frame);
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
    int ret = msg_event_alloc(buf, &event);
    if (ret != SUCCESS)
    {
        LOG_WRN("Failed to allocate rx event: %d", ret);
        net_buf_unref(buf);
        return len;
    }
    PASS_OWNERSHIP(buf);

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
    uint32_t key = irq_lock();
    struct bt_conn* conn = current_conn != NULL ? bt_conn_ref(current_conn) : NULL;
    irq_unlock(key);
    if (conn == NULL)
    {
        return -ENOTCONN;
    }
    int ret = ble_notify(conn, data, len);
    bt_conn_unref(conn);
    return ret;
}

static int ble_notify(struct bt_conn* conn, const uint8_t* data, uint16_t len)
{
    if (data == NULL || len == 0U)
    {
        return -EINVAL;
    }

    uint16_t mtu = bt_gatt_get_mtu(conn);
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

        int ret = bt_gatt_notify(conn, tx_attr, &data[offset], chunk);
        if (ret != 0)
        {
            LOG_WRN("bt_gatt_notify failed at offset %u: %d", offset, ret);
            return ret;
        }
        offset += chunk;
    }

    return SUCCESS;
}

static void ble_interface_init(void) { LOG_INF("BLE interface initialized"); }

static void ble_interface_thread(void* arg1, void* arg2, void* arg3)
{
    ARG_UNUSED(arg1);
    ARG_UNUSED(arg2);
    ARG_UNUSED(arg3);

    int ret;
    event_t* event = NULL;
    struct net_buf* frame = NULL;

    struct k_poll_event poll_events[2] = {
        K_POLL_EVENT_STATIC_INITIALIZER(
            K_POLL_TYPE_MSGQ_DATA_AVAILABLE, K_POLL_MODE_NOTIFY_ONLY, &ble_interface_queue, 0),
        K_POLL_EVENT_STATIC_INITIALIZER(K_POLL_TYPE_MSGQ_DATA_AVAILABLE, K_POLL_MODE_NOTIFY_ONLY, &ble_tx_queue, 0),
    };

    LOG_INF("BLE interface thread started");

    while (true)
    {
        k_poll(poll_events, ARRAY_SIZE(poll_events), K_FOREVER);

        while (k_msgq_get(&ble_interface_queue, (void*)&event, K_NO_WAIT) == 0)
        {
            ASSERT(event->type == &msg_event, "Unexpected event type");

            struct msg_event_payload* payload = (struct msg_event_payload*)event->data.buf;
            ret = stow_cobs_transport_feed(&ble_link, payload->buf->data, payload->buf->len);
            if (ret != SUCCESS)
            {
                LOG_WRN("Failed to receive BLE frame: %d", ret);
            }

            EVENT_UNREF(&event);
        }

        while (k_msgq_get(&ble_tx_queue, (void*)&frame, K_NO_WAIT) == 0)
        {
            ret = ble_interface_tx(frame->data, frame->len);
            if (ret != SUCCESS && ret != -ENOTCONN)
            {
                LOG_WRN("Failed to transmit over BLE: %d", ret);
            }
            net_buf_unref(frame);
        }

        poll_events[0].state = K_POLL_STATE_NOT_READY;
        poll_events[1].state = K_POLL_STATE_NOT_READY;
    }
}

//**********************************************************
//* Public Function Definitions
//**********************************************************
