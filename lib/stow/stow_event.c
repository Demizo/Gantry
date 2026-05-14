/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @author Demizo (demizodemazo@gmail.com)
 * @brief Stow update event definition
 *
 *
 */

#include <autoconf.h>
#include <gantry/error.h>
#include <gantry/event.h>
#include <gantry/stow/stow_event.h>
#include <sys/errno.h>

/**
 * @brief Logger for module
 */
LOG_MODULE_REGISTER(stow_event, CONFIG_STOW_LOG_LEVEL);

//**********************************************************
//* Local Definitions
//**********************************************************

/**
 * @brief Define the stow update event
 */
DEFINE_EVENT_TYPE(EVENT_ID_STOW_UPDATE, stow_update_event, stow_event_on_free);

//**********************************************************
//* Static Function Declarations
//**********************************************************

//**********************************************************
//* Static Variable Definitions
//**********************************************************

//**********************************************************
//* Static Function Definitions
//**********************************************************

//**********************************************************
//* Public Function Definitions
//**********************************************************

void stow_event_on_free(event_t* event)
{
    ASSERT(event->type->id == EVENT_ID_STOW_UPDATE, "Unexpected event type %d", event->type->id);
    struct stow_update_event_payload* payload = (struct stow_update_event_payload*)event->data.buf;
    if (payload->mode == STOW_SUBSCRIPTION_COPY)
    {
        payload->metadata->interface->release(&(payload->value_copy));
    }
}
