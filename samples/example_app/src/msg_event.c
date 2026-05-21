/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Message event
 *
 * @author Demizo (demizodemazo@gmail.com)
 */

#include "msg_event.h"

#include <gantry/error.h>
#include <gantry/event.h>
#include <gantry/memory.h>

//**********************************************************
//* Local Definitions
//**********************************************************

//**********************************************************
//* Static Function Declarations
//**********************************************************

static void on_free(event_t* event);

//**********************************************************
//* Static Variable Definitions
//**********************************************************

DEFINE_EVENT_TYPE(EVENT_ID_MSG, msg_event, on_free);

//**********************************************************
//* Static Function Definitions
//**********************************************************

static void on_free(event_t* event)
{
    struct msg_event_payload* payload = (struct msg_event_payload*)event->data.buf;
    if (payload->buf != NULL)
    {
        net_buf_unref(payload->buf);
        payload->buf = NULL;
    }
}

//**********************************************************
//* Public Function Definitions
//**********************************************************

int msg_event_alloc(enum msg_medium medium, enum msg_direction direction, struct net_buf* buf, event_t** event_ptr)
{
    int ret = EVENT_ALLOC(&msg_event, sizeof(struct msg_event_payload), event_ptr);
    if (ret != SUCCESS)
    {
        NOT_REFERENCED(event_ptr);
        return ret;
    }

    struct msg_event_payload* payload = (struct msg_event_payload*)(*event_ptr)->data.buf;
    payload->medium = medium;
    payload->direction = direction;
    payload->buf = buf;

    PASS_OWNERSHIP(event_ptr);
    return SUCCESS;
}
