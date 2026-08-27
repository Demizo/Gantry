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

//**********************************************************
//* Functions
//**********************************************************
