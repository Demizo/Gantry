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

#include <zephyr/net_buf.h>
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

//**********************************************************
//* Functions
//**********************************************************

/**
 * @brief Transmit a message over BLE
 *
 * @param buf Message buffer to send
 */
void ble_interface_send(struct net_buf* buf);

/**
 * @brief Initialize the BLE interface
 */
void ble_interface_init(void);

/**
 * @brief BLE interface thread
 *
 * @details Send and receive messages over BLE
 *
 * @param arg1 Unused
 * @param arg2 Unused
 * @param arg3 Unused
 */
void ble_interface_thread(void* arg1, void* arg2, void* arg3);
