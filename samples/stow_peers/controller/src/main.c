/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief The controller: watches the sensor's battery, and puts the sensor in ECO mode when it drops low
 *
 * @author Demizo (demizodemazo@gmail.com)
 */

#include <gantry/error.h>
#include <gantry/event.h>
#include <gantry/module.h>
#include <generated_sensor_stow_client.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "peer.h"

LOG_MODULE_REGISTER(controller_app, LOG_LEVEL_INF);

//**********************************************************
//* Local Definitions
//**********************************************************

#define LOW_BATTERY 20
#define QUEUE_DEPTH 4U
#define STACK_SIZE 2048U
#define PRIORITY 9

/**
 * @brief A client event for the controller thread: a connected peer or an Update
 */
struct controller_msg
{
    struct stow_client_peer* peer; /**< A newly connected sensor, owned by the message, or NULL */
    event_t* update;               /**< An Update, owned by the message, or NULL */
};

//**********************************************************
//* Static Function Declarations
//**********************************************************

static void on_connected(struct stow_client_peer* peer, int err);
static void on_update(event_t* event);
static void check_battery(struct stow_client_peer* sensor, int level);
static void watch_battery(struct stow_client_peer* sensor);
static void controller_app_init(void);
static void controller_app_thread(void* arg1, void* arg2, void* arg3);

//**********************************************************
//* Static Variable Definitions
//**********************************************************

K_MSGQ_DEFINE(controller_queue, sizeof(struct controller_msg), QUEUE_DEPTH, sizeof(void*));

/**
 * @brief The sensor's last battery level, used only by the controller thread
 */
static int last_battery = LOW_BATTERY;

STOW_CLIENT_ENDPOINT_DEFINE(peer_client, &sensor_stow_client_schema, 0);

STOW_CLIENT_CB_DEFINE(controller_hooks) = {
    .connected = on_connected,
    .update = on_update,
};

GANTRY_MODULE_DEFINE(controller_app, controller_app_init, controller_app_thread, STACK_SIZE, PRIORITY);

//**********************************************************
//* Static Function Definitions
//**********************************************************

static void on_connected(struct stow_client_peer* peer, int err)
{
    if (err != SUCCESS)
    {
        return;
    }
    struct stow_client_peer* sensor = stow_client_peer_ref(peer);
    const struct controller_msg msg = { .peer = sensor };
    if (k_msgq_put(&controller_queue, &msg, K_NO_WAIT) != 0)
    {
        stow_client_peer_unref(&sensor);
    }
    else
    {
        PASS_OWNERSHIP(sensor);
    }
}

static void on_update(event_t* event)
{
    EVENT_REF(event);
    const struct controller_msg msg = { .update = event };
    if (k_msgq_put(&controller_queue, &msg, K_NO_WAIT) != 0)
    {
        EVENT_UNREF(&event);
    }
    else
    {
        PASS_OWNERSHIP(event);
    }
}

static void check_battery(struct stow_client_peer* sensor, int level)
{
    bool dropped_low = (level < LOW_BATTERY) && (last_battery >= LOW_BATTERY);
    last_battery = level;
    if (!dropped_low)
    {
        return;
    }
    const data_value_t eco = { .type = STOW_ITEM_TYPE_ENUM, .data.int_value = sensor_Mode_ECO };
    int ret = stow_client_set_one(sensor, SENSOR_ID_MODE, eco);
    if (ret != SUCCESS)
    {
        LOG_WRN("Could not put the sensor in ECO mode: %d", ret);
    }
}

static void watch_battery(struct stow_client_peer* sensor)
{
    last_battery = LOW_BATTERY;
    int ret = stow_client_subscribe_one(sensor, SENSOR_ID_BATTERY);
    if (ret != SUCCESS)
    {
        LOG_WRN("Could not subscribe to the sensor's battery: %d", ret);
        return;
    }
    struct stow_client_value level;
    ret = stow_client_get_one(sensor, SENSOR_ID_BATTERY, &level);
    if (ret != SUCCESS)
    {
        LOG_WRN("Could not read the sensor's battery: %d", ret);
        return;
    }
    LOG_INF("Watching the sensor's battery, at %d%%", level.value.data.int_value);
    check_battery(sensor, level.value.data.int_value);
    stow_client_release(&peer_client, &level);
}

static void controller_app_init(void) {}

static void controller_app_thread(void* arg1, void* arg2, void* arg3)
{
    ARG_UNUSED(arg1);
    ARG_UNUSED(arg2);
    ARG_UNUSED(arg3);

    while (true)
    {
        struct controller_msg msg;
        k_msgq_get(&controller_queue, &msg, K_FOREVER);
        struct stow_client_peer* sensor = msg.peer;
        event_t* event = msg.update;
        if (sensor != NULL)
        {
            watch_battery(sensor);
            stow_client_peer_unref(&sensor);
        }
        else
        {
            const struct stow_client_update_payload* update = (const void*)event->data.buf;
            if (update->value.id == SENSOR_ID_BATTERY)
            {
                check_battery(update->peer, update->value.value.data.int_value);
            }
            EVENT_UNREF(&event);
        }
    }
}
