/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief UART Interface
 *
 * @details Handles sending and receiving messages over UART.
 *
 * @author Demizo (demizodemazo@gmail.com)
 */

#pragma once

#include <zephyr/kernel.h>
#include <zephyr/types.h>

//**********************************************************
//* Definitions
//**********************************************************

//**********************************************************
//* Typedefs, Enums, and Structs
//**********************************************************

/**
 * @brief Encoded frames waiting to be sent over UART, as `struct net_buf*`
 */
extern struct k_msgq uart_tx_queue;

//**********************************************************
//* Functions
//**********************************************************
