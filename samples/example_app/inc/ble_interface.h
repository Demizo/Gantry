/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief BLE Interface
 *
 * @details Handles sending and receiving messages over BLE.
 *
 * @author Demizo (demizodemazo@gmail.com)
 */

#pragma once

#include <zephyr/kernel.h>
#include <zephyr/types.h>

//**********************************************************
//* Definitions
//**********************************************************

#define BT_UUID_NUS_VAL BT_UUID_128_ENCODE(0x6e400001, 0xb5a3, 0xf393, 0xe0a9, 0xe50e24dcca9e)
#define BT_UUID_NUS_RX_VAL BT_UUID_128_ENCODE(0x6e400002, 0xb5a3, 0xf393, 0xe0a9, 0xe50e24dcca9e)
#define BT_UUID_NUS_TX_VAL BT_UUID_128_ENCODE(0x6e400003, 0xb5a3, 0xf393, 0xe0a9, 0xe50e24dcca9e)

#define BT_UUID_NUS BT_UUID_DECLARE_128(BT_UUID_NUS_VAL)
#define BT_UUID_NUS_RX BT_UUID_DECLARE_128(BT_UUID_NUS_RX_VAL)
#define BT_UUID_NUS_TX BT_UUID_DECLARE_128(BT_UUID_NUS_TX_VAL)

//**********************************************************
//* Typedefs, Enums, and Structs
//**********************************************************

/**
 * @brief Encoded frames waiting to be sent over BLE, as `struct net_buf*`
 */
extern struct k_msgq ble_tx_queue;

//**********************************************************
//* Functions
//**********************************************************
