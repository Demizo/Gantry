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

struct msg_event_payload
{
    struct net_buf* buf;
};

DECLARE_EVENT_TYPE(msg_event);

//**********************************************************
//* Functions
//**********************************************************

/**
 * @brief Allocate a message event.
 *
 * @param[in] buf net_buf containing the message
 * @param[out] event_ptr Populated with the allocated event on success
 *
 * @return SUCCESS or a negative error code.
 * @acquires memory event_ptr on-success
 */
int msg_event_alloc(struct net_buf* buf, event_t** event_ptr);
