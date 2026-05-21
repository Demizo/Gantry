/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief BLE Manager
 *
 * @details Manages BLE connections.
 *
 * @author Demizo (demizodemazo@gmail.com)
 */

#pragma once

#include <zephyr/types.h>

//**********************************************************
//* Definitions
//**********************************************************

//**********************************************************
//* Typedefs, Enums, and Structs
//**********************************************************

//**********************************************************
//* Functions
//**********************************************************

/**
 * @brief Initialize the BLE Manager
 *
 * @details Enables BLE, sets the device name, and begins advertising.
 */
void ble_manager_init(void);

/**
 * @brief BLE Manager thread
 *
 * @param arg1 Unused
 * @param arg2 Unused
 * @param arg3 Unused
 */
void ble_manager_thread(void* arg1, void* arg2, void* arg3);
