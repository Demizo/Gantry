/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Message event
 *
 * @details Event type for messages sent and received by the application
 *
 * @author Demizo (demizodemazo@gmail.com)
 *
 */

#pragma once

#include <gantry/event.h>
#include <zephyr/net_buf.h>

//**********************************************************
//* Definitions
//**********************************************************

#define EVENT_ID_MSG 0xffff0001

//**********************************************************
//* Typedefs, Enums, and Structs
//**********************************************************

enum msg_medium
{
    MSG_MEDIUM_UART = 0,
    MSG_MEDIUM_BLE,
};

enum msg_direction
{
    MSG_DIRECTION_RX = 0,
    MSG_DIRECTION_TX,
};

struct msg_event_payload
{
    enum msg_medium medium;
    enum msg_direction direction;
    struct net_buf* buf;
};

DECLARE_EVENT_TYPE(msg_event);

//**********************************************************
//* Functions
//**********************************************************

/**
 * @brief Allocate a message event.
 *
 * @param[in] medium Transport medium
 * @param[in] direction Direction of the message
 * @param[in] buf net_buf containing the message
 * @param[out] event_ptr Populated with the allocated event on success
 *
 * @return SUCCESS or a negative error code.
 */
int msg_event_alloc(enum msg_medium medium, enum msg_direction direction, struct net_buf* buf, event_t** event_ptr);
