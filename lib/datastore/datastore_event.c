/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @author Demizo (demizodemazo@gmail.com)
 * @brief Datastore update event definition
 *
 *
 */

#include <autoconf.h>
#include <sys/errno.h>
#include <zds/datastore/datastore_event.h>
#include <zds/error.h>
#include <zds/event.h>

/**
 * @brief Logger for module
 */
LOG_MODULE_REGISTER(datastore_event, CONFIG_DATASTORE_LOG_LEVEL);

//**********************************************************
//* Local Definitions
//**********************************************************

/**
 * @brief Define the datastore update event
 */
DEFINE_EVENT_TYPE(EVENT_ID_DATASTORE_UPDATE, datastore_update_event, datastore_event_on_free);

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

void datastore_event_on_free(event_t* event)
{
    ASSERT(event->type->id == EVENT_ID_DATASTORE_UPDATE, "Unexpected event type %d", event->type->id);
    struct datastore_update_event_payload* payload = (struct datastore_update_event_payload*)event->data.buf;
    if (payload->mode == DATASTORE_SUBSCRIPTION_COPY)
    {
        payload->metadata->interface->release(&(payload->value_copy));
    }
}
