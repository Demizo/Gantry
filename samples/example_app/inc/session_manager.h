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

#pragma once

#include <gantry/stow/stow_cobs_transport.h>
#include <stdint.h>
#include <zephyr/net_buf.h>

//**********************************************************
//* Definitions
//**********************************************************

#define UART_SESSION_ID 1U
#define UART_SESSION_AUTH STOW_ROLE_ANY

#define BLE_SESSION_ID 2U
#define BLE_SESSION_AUTH STOW_ROLE_SESSION

//**********************************************************
//* Typedefs, Enums, and Structs
//**********************************************************

/**
 * @brief Raw COBS transport for the UART link
 */
extern struct stow_cobs_transport uart_link;

/**
 * @brief Raw COBS transport for the BLE link
 */
extern struct stow_cobs_transport ble_link;

//**********************************************************
//* Functions
//**********************************************************
