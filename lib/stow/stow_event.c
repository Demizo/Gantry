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

const struct stow_update_event_payload* stow_update_payload(const event_t* event)
{
    ASSERT(event->type == &stow_update_event, "Unexpected event type %d", event->type->id);
    return (const struct stow_update_event_payload*)event->data.buf;
}

void stow_event_on_free(event_t* event)
{
    const struct stow_update_event_payload* payload = stow_update_payload(event);
    if (payload->mode == STOW_SUBSCRIPTION_COPY)
    {
        data_value_t value_copy = payload->value_copy;
        payload->metadata->interface->release(&value_copy);
    }
}
