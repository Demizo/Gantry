/**
 * @file event.c
 * @author Demizo (demizodemazo@gmail.com)
 * @brief Event implementation
 * @version 0.1
 * @date 2026-03-01
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "event.h"

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "error.h"
#include "memory.h"

LOG_MODULE_REGISTER(event, CONFIG_EVENT_LOG_LEVEL);

//**********************************************************
//* Local Definitions
//**********************************************************

// The maximum depth of linked events
#define LINKED_EVENT_CHAIN_LEN_MAX 10

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

int event_alloc(size_t size, event_direction_t direction, event_format_t format, event_t** event _ALLOC_TRACE)
{
    int ret = SUCCESS;

    // Account for event header size in total size
    size_t total_size = sizeof(event_t) + size;
    ret = mem_alloc(total_size, (void**)event _ALLOC_TRACE_PASSTHROUGH);
    if (ret)
    {
        LOG_ERR("Failed to allocate event: %d", ret);
        return ret;
    }

    event_init(*event, size, direction, format);

    LOG_DBG("Event allocated: size: %zu, direction: %d, format: %d", size, direction, format);
    return SUCCESS;
}

int event_ref(event_t* event _REF_TRACE)
{
    int ret = SUCCESS;
    event_t* current = NULL;

    // Iterate over linked events, incrementing the reference count
    current = event;
    while (current != NULL)
    {
        ret = mem_ref(current _REF_TRACE_PASSTHROUGH);
        if (ret)
        {
            LOG_ERR("Failed to reference event: %d", ret);
            return ret;
        }

        current = current->next_event;
    }

    LOG_DBG("Incremented reference count for event");
    return SUCCESS;
}

int event_unref(event_t** event _REF_TRACE)
{
    int ret = SUCCESS;
    event_t* current = NULL;
    event_t* next = NULL;
    int chain_length = 0;

    // Iterate over linked events, decrementing the reference count
    current = *event;
    while (current != NULL && (chain_length < LINKED_EVENT_CHAIN_LEN_MAX))
    {
        // Get the linked event first in case the block is freed
        next = current->next_event;

        ret = mem_unref((void**)&current _REF_TRACE_PASSTHROUGH);
        if (ret)
        {
            LOG_ERR("Failed to dereference event: %d", ret);
            return ret;
        }

        current = next;

        chain_length++;
    }

    LOG_DBG("Decremented reference count for event");
    return SUCCESS;
}

void event_init(event_t* event, size_t size, event_direction_t direction, event_format_t format)
{
    event->next_event = NULL;
    event->return_queue = NULL;
    event->direction = direction;
    event->format = format;
    event->data.len = size;
}
