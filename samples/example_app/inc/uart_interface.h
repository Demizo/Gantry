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

#include <zephyr/net_buf.h>
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
 * @brief Transmit a message over UART
 *
 * @param buf Message buffer to send
 */
void uart_interface_send(struct net_buf* buf);
