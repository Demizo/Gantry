/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Entry point for the example app
 *
 * @author Demizo (demizodemazo@gmail.com)
 */

#include <gantry/stow/stow.h>
#include <zephyr/app_version.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net_buf.h>

#include "ble_interface.h"
#include "ble_manager.h"
#include "session_manager.h"
#include "uart_interface.h"

/**
 * @brief Logger for module
 */
LOG_MODULE_REGISTER(main, CONFIG_MAIN_LOG_LEVEL);

//**********************************************************
//* Local Definitions
//**********************************************************

//**********************************************************
//* Static Function Declarations
//**********************************************************

//**********************************************************
//* Static Variable Definitions
//**********************************************************

K_THREAD_STACK_DEFINE(uart_interface_stack, CONFIG_UART_INTERFACE_STACK_SIZE);
static struct k_thread uart_interface_thread_data;

K_THREAD_STACK_DEFINE(ble_interface_stack, CONFIG_BLE_INTERFACE_STACK_SIZE);
static struct k_thread ble_interface_thread_data;

K_THREAD_STACK_DEFINE(ble_manager_stack, CONFIG_BLE_MANAGER_STACK_SIZE);
static struct k_thread ble_manager_thread_data;

//**********************************************************
//* Static Function Definitions
//**********************************************************

//**********************************************************
//* Public Function Definitions
//**********************************************************

int main(void)
{
    LOG_INF("Starting Gantry example v%s", APP_VERSION_STRING);

    stow_init();
    session_manager_init();
    uart_interface_init();
    ble_manager_init();
    ble_interface_init();

    LOG_INF("Starting app threads");

    k_thread_create(
        &uart_interface_thread_data, uart_interface_stack, K_THREAD_STACK_SIZEOF(uart_interface_stack),
        uart_interface_thread, NULL, NULL, NULL, CONFIG_UART_INTERFACE_THREAD_PRIORITY, 0, K_NO_WAIT);
    k_thread_name_set(&uart_interface_thread_data, "uart_interface");

    k_thread_create(
        &ble_interface_thread_data, ble_interface_stack, K_THREAD_STACK_SIZEOF(ble_interface_stack),
        ble_interface_thread, NULL, NULL, NULL, CONFIG_BLE_INTERFACE_THREAD_PRIORITY, 0, K_NO_WAIT);
    k_thread_name_set(&ble_interface_thread_data, "ble_interface");

    k_thread_create(
        &ble_manager_thread_data, ble_manager_stack, K_THREAD_STACK_SIZEOF(ble_manager_stack), ble_manager_thread, NULL,
        NULL, NULL, CONFIG_BLE_MANAGER_THREAD_PRIORITY, 0, K_NO_WAIT);
    k_thread_name_set(&ble_manager_thread_data, "ble_manager");

    return 0;
}
