/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief BLE Manager
 *
 * @author Demizo (demizodemazo@gmail.com)
 *
 */

#include "ble_manager.h"

#include <gantry/error.h>
#include <gantry/event.h>
#include <gantry/flags.h>
#include <gantry/module.h>
#include <gantry/stow/stow.h>
#include <gantry/stow/stow_event.h>
#include <gantry/stow/stow_protocol.h>
#include <generated_stow_enums.h>
#include <generated_stow_items.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "ble_interface.h"
#include "session_manager.h"
#include "zephyr/bluetooth/addr.h"
#include "zephyr/bluetooth/hci_types.h"

/**
 * @brief Logger for module
 */
LOG_MODULE_REGISTER(ble_manager, CONFIG_BLE_MANAGER_LOG_LEVEL);

//**********************************************************
//* Local Definitions
//**********************************************************

//**********************************************************
//* Static Function Declarations
//**********************************************************

static void on_stow_update(event_t* event);
static void on_connected(struct bt_conn* conn, uint8_t err);
static void on_disconnected(struct bt_conn* conn, uint8_t reason);
static void on_recycled(void);
static int update_advertising_data(void);
static int start_advertising(void);
static void adv_restart_work_handler(struct k_work* work);
static void apply_current_device_name(void);
static void handle_update(event_t* event);
static void ble_manager_init(void);
static void ble_manager_thread(void* arg1, void* arg2, void* arg3);

//**********************************************************
//* Static Variable Definitions
//**********************************************************

FLAGS_DEFINE(ble_manager_flags, (INITIALIZED, CONNECTED), { FLAG_REQUIRES(CONNECTED, INITIALIZED); });
K_MSGQ_DEFINE(ble_manager_queue, sizeof(event_t*), CONFIG_BLE_MANAGER_QUEUE_DEPTH, sizeof(void*));

// Extended advertising set
static struct bt_le_ext_adv* adv_set = NULL;

// Work for restarting advertising
K_WORK_DEFINE(adv_restart_work, adv_restart_work_handler);

// Callbacks for tracking BLE connection state
BT_CONN_CB_DEFINE(ble_conn_cb) = {
    .connected = on_connected,
    .disconnected = on_disconnected,
    .recycled = on_recycled,
};

// The current BLE connection
static struct bt_conn* current_conn;

// Stow subscriptions
STOW_SUBSCRIPTION_DEFINE(
    ble_manager_stow_sub, STOW_SUBSCRIPTION_HANDLE, on_stow_update, STOW_ID_DEVICE_NAME, STOW_ID_BLE_CONNECTION_STATE);

// Module registration
GANTRY_MODULE_DEFINE(
    ble_manager, ble_manager_init, ble_manager_thread, CONFIG_BLE_MANAGER_STACK_SIZE,
    CONFIG_BLE_MANAGER_THREAD_PRIORITY);

//**********************************************************
//* Static Function Definitions
//**********************************************************

static void on_stow_update(event_t* event)
{
    if (k_msgq_put(&ble_manager_queue, (void*)&event, K_NO_WAIT) != 0)
    {
        LOG_WRN("Event dropped, queue full");
        EVENT_UNREF(&event);
        return;
    }
}

static void on_connected(struct bt_conn* conn, uint8_t err)
{
    int ret;
    char address[BT_ADDR_LE_STR_LEN];

    if (err != 0)
    {
        LOG_WRN("Connection failed: %u", err);
        return;
    }

    // Update the connection state
    data_value_t value = {
        .type = STOW_ITEM_TYPE_ENUM,
        .data = { .int_value = BleConnectionState_CONNECTED },
    };
    (void)STOW_SET(STOW_ROLE_INTERNAL, STOW_ID_BLE_CONNECTION_STATE, value);

    // Open the Stow session
    ret = stow_protocol_session_open(BLE_SESSION_ID, BLE_SESSION_AUTH);
    if (ret != SUCCESS)
    {
        LOG_ERR("Failed to open BLE session: %d", ret);
    }

    current_conn = bt_conn_ref(conn);

    bt_addr_le_to_str(bt_conn_get_dst(conn), address, sizeof(address));
    LOG_INF("Connected (%s)", address);

    SET_FLAG(ble_manager_flags, CONNECTED);
}

static void on_disconnected(struct bt_conn* conn, uint8_t reason)
{
    int ret;
    char address[BT_ADDR_LE_STR_LEN];

    // Update the connection state
    data_value_t value = {
        .type = STOW_ITEM_TYPE_ENUM,
        .data = { .int_value = BleConnectionState_DISCONNECTED },
    };
    (void)STOW_SET(STOW_ROLE_INTERNAL, STOW_ID_BLE_CONNECTION_STATE, value);

    // Close the Stow session
    ret = stow_protocol_session_closed(BLE_SESSION_ID);
    if (ret != SUCCESS)
    {
        LOG_ERR("Failed to close BLE session: %d", ret);
    }

    if (current_conn != NULL)
    {
        bt_conn_unref(current_conn);
        current_conn = NULL;
    }

    bt_addr_le_to_str(bt_conn_get_dst(conn), address, sizeof(address));
    LOG_INF("Disconnected (%s), reason: %d", address, reason);

    CLEAR_FLAG(ble_manager_flags, CONNECTED);
}

static void on_recycled(void) { k_work_submit(&adv_restart_work); }

static int update_advertising_data(void)
{
    const char* name = bt_get_name();
    const struct bt_data advertising_data[] = {
        BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
        BT_DATA_BYTES(BT_DATA_UUID128_ALL, BT_UUID_NUS_VAL),
        BT_DATA(BT_DATA_NAME_COMPLETE, name, strlen(name)),
    };

    return bt_le_ext_adv_set_data(adv_set, advertising_data, ARRAY_SIZE(advertising_data), NULL, 0);
}

static int start_advertising(void)
{
    int ret = update_advertising_data();
    if (ret != SUCCESS)
    {
        LOG_ERR("Failed to set advertising data: %d", ret);
        return ret;
    }

    ret = bt_le_ext_adv_start(adv_set, BT_LE_EXT_ADV_START_DEFAULT);
    if (ret != SUCCESS)
    {
        LOG_ERR("Failed to start advertising: %d", ret);
        return ret;
    }

    LOG_INF("Advertising as \"%s\"", bt_get_name());
    return SUCCESS;
}

static void adv_restart_work_handler(struct k_work* work)
{
    ARG_UNUSED(work);
    LOG_DBG("Restarting advertising");
    (void)start_advertising();
}

static void apply_current_device_name(void)
{
    data_value_t value;
    int ret = STOW_GET(STOW_ROLE_INTERNAL, STOW_ID_DEVICE_NAME, &value);
    if (ret == SUCCESS)
    {
        int set_ret = bt_set_name(value.data.string_value);
        if (set_ret != 0)
        {
            LOG_ERR("Failed to set device name: %d", set_ret);
        }
    }
    else
    {
        LOG_WRN("Failed to get current device name: %d", ret);
    }

    STOW_RELEASE(STOW_ID_DEVICE_NAME, &value);
}

static void handle_update(event_t* event)
{
    const struct stow_update_event_payload* payload = (const struct stow_update_event_payload*)event->data.buf;
    const enum stow_item_id id = (enum stow_item_id)payload->metadata->id;

    switch (id)
    {
        case STOW_ID_DEVICE_NAME:
        {
            apply_current_device_name();

            if (!CHECK_FLAG(ble_manager_flags, CONNECTED))
            {
                int ret = update_advertising_data();
                if (ret != SUCCESS)
                {
                    LOG_ERR("Failed to set advertising data: %d", ret);
                    break;
                }

                LOG_INF("Updated name \"%s\"", bt_get_name());
            }

            break;
        }
        case STOW_ID_BLE_CONNECTION_STATE:
        {
            if ((!CHECK_FLAG(ble_manager_flags, CONNECTED)) || (current_conn == NULL))
            {
                // Already disconnected, nothing to do
                break;
            }

            // Check if a disconnect was initiated
            data_value_t value;
            int ret = STOW_GET(STOW_ROLE_INTERNAL, STOW_ID_BLE_CONNECTION_STATE, &value);
            if (ret == SUCCESS)
            {
                if (value.data.int_value == BleConnectionState_DISCONNECTED)
                {
                    // Trigger a disconnect
                    bt_conn_disconnect(current_conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
                }
            }

            STOW_RELEASE(STOW_ID_BLE_CONNECTION_STATE, &value);
            break;
        }
        default:
        {
            LOG_WRN("Unexpected item (%d)", id);
            break;
        }
    }
}

static void ble_manager_init(void)
{
    int ret;

    ret = bt_enable(NULL);
    if (ret != 0)
    {
        LOG_ERR("Failed to enable BLE: %d", ret);
        return;
    }

    // Set the device name
    apply_current_device_name();

    // Create the advertising settings
    struct bt_le_adv_param adv_param = {
        .options = (BT_LE_ADV_OPT_EXT_ADV | BT_LE_ADV_OPT_CONN),
        .interval_min = BT_GAP_ADV_FAST_INT_MIN_2,
        .interval_max = BT_GAP_ADV_FAST_INT_MAX_2,
        .peer = NULL,
    };
    ret = bt_le_ext_adv_create(&adv_param, NULL, &adv_set);
    if (ret != 0)
    {
        LOG_ERR("Failed to create advertising set: %d", ret);
        return;
    }

    SET_FLAG(ble_manager_flags, INITIALIZED);

    LOG_INF("BLE manager initialized");
}

static void ble_manager_thread(void* arg1, void* arg2, void* arg3)
{
    ARG_UNUSED(arg1);
    ARG_UNUSED(arg2);
    ARG_UNUSED(arg3);

    k_timepoint_t timeout_timepoint = sys_timepoint_calc(K_FOREVER);
    event_t* event = NULL;

    LOG_INF("BLE manager thread started");

    // Begin advertising
    (void)start_advertising();

    while (true)
    {
        if (k_msgq_get(&ble_manager_queue, (void*)&event, sys_timepoint_timeout(timeout_timepoint)) == 0)
        {
            switch (event->type->id)
            {
                case EVENT_ID_STOW_UPDATE:
                {
                    handle_update(event);
                    break;
                }
                default:
                {
                    LOG_WRN("Unexpected event id %u", event->type->id);
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
