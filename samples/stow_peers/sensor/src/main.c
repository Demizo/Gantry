/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief The sensor: drains its battery, and reads the controller's name when the controller connects
 *
 * @author Demizo (demizodemazo@gmail.com)
 */

#include <gantry/error.h>
#include <gantry/module.h>
#include <gantry/stow/stow.h>
#include <generated_controller_stow_client.h>
#include <generated_stow_items.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "peer.h"

LOG_MODULE_REGISTER(sensor_app, LOG_LEVEL_INF);

//**********************************************************
//* Local Definitions
//**********************************************************

#define DRAIN_PERIOD K_SECONDS(1)
#define QUEUE_DEPTH 2U
#define STACK_SIZE 2048U
#define PRIORITY 9

//**********************************************************
//* Static Function Declarations
//**********************************************************

static void on_connected(struct stow_client_peer* peer, int err);
static void read_name(struct stow_client_peer* controller);
static void drain_battery(void);
static void sensor_app_init(void);
static void sensor_app_thread(void* arg1, void* arg2, void* arg3);

//**********************************************************
//* Static Variable Definitions
//**********************************************************

K_MSGQ_DEFINE(sensor_queue, sizeof(struct stow_client_peer*), QUEUE_DEPTH, sizeof(void*));

STOW_CLIENT_ENDPOINT_DEFINE(peer_client, &controller_stow_client_schema, 0);

STOW_CLIENT_CB_DEFINE(sensor_hooks) = {
    .connected = on_connected,
};

GANTRY_MODULE_DEFINE(sensor_app, sensor_app_init, sensor_app_thread, STACK_SIZE, PRIORITY);

//**********************************************************
//* Static Function Definitions
//**********************************************************

static void on_connected(struct stow_client_peer* peer, int err)
{
    if (err != SUCCESS)
    {
        return;
    }
    struct stow_client_peer* controller = stow_client_peer_ref(peer);
    if (k_msgq_put(&sensor_queue, (const void*)&controller, K_NO_WAIT) != 0)
    {
        stow_client_peer_unref(&controller);
    }
    else
    {
        PASS_OWNERSHIP(controller);
    }
}

static void read_name(struct stow_client_peer* controller)
{
    struct stow_client_value name;
    int ret = stow_client_get_one(controller, CONTROLLER_ID_NAME, &name);
    if (ret != SUCCESS)
    {
        LOG_WRN("Could not read the controller's name: %d", ret);
        return;
    }
    LOG_INF("Paired with controller %s", name.value.data.string_value);
    stow_client_release(&peer_client, &name);
}

static void drain_battery(void)
{
    data_value_t level;
    if (stow_get(STOW_ID_BATTERY, &level) != SUCCESS)
    {
        return;
    }
    level.data.int_value = (level.data.int_value > 0) ? level.data.int_value - 1 : 100;
    (void)stow_set(STOW_ID_BATTERY, level);
    stow_release(STOW_ID_BATTERY, &level);
}

static void sensor_app_init(void) {}

static void sensor_app_thread(void* arg1, void* arg2, void* arg3)
{
    ARG_UNUSED(arg1);
    ARG_UNUSED(arg2);
    ARG_UNUSED(arg3);

    while (true)
    {
        struct stow_client_peer* controller = NULL;
        if (k_msgq_get(&sensor_queue, (void*)&controller, DRAIN_PERIOD) == 0)
        {
            read_name(controller);
            stow_client_peer_unref(&controller);
        }
        else
        {
            drain_battery();
        }
    }
}
