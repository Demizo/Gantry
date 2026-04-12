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

/**
 * @brief Logger for module
 */
LOG_MODULE_REGISTER(event, CONFIG_EVENT_LOG_LEVEL);

//**********************************************************
//* Local Definitions
//**********************************************************

/**
 * @brief Max depth for linked event chains
 */
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

int event_alloc(size_t size, event_direction_t direction, event_type_t* type, event_t** event)
{
    int ret = SUCCESS;

    // Account for event header size in total size
    size_t total_size = sizeof(event_t) + size;
    void** event_block = (void**)event;
    ret = mem_alloc(total_size, event_block);
    if (ret)
    {
        LOG_ERR("Failed to allocate event: %d", ret);

        NOT_REFERENCED(event_block);
        return ret;
    }

    event_init(*event, size, direction, type);

    LOG_DBG("Event allocated: size: %zu, direction: %d, type: %d", size, direction, type->id);

    PASS_OWNERSHIP(event_block);
    return SUCCESS;
}

int event_ref(event_t* event)
{
    int ret = SUCCESS;
    event_t* current = NULL;

    // Iterate over linked events, incrementing the reference count
    current = event;
    while (current != NULL)
    {
        ret = mem_ref(current);
        if (ret)
        {
            LOG_ERR("Failed to reference event: %d", ret);

            NOT_REFERENCED(current);
            return ret;
        }

        current = current->next_event;
    }

    LOG_DBG("Incremented reference count for event");

    PASS_OWNERSHIP(current);
    return SUCCESS;
}

int event_unref(event_t** event)
{
    int ret = SUCCESS;
    event_t* current = NULL;
    event_t* next = NULL;
    int chain_length = 0;

    // Iterate over linked events, decrementing the reference count
    current = *event;
    while (current != NULL && (chain_length < LINKED_EVENT_CHAIN_LEN_MAX))
    {
        // If the event will be freed, call on_free if defined for the current event type
        if (mem_get_ref_count((void*)current) == 1 && current->type->on_free != NULL)
        {
            current->type->on_free(current);
        }

        // Get the linked event first in case the block is freed
        next = current->next_event;

        void** current_block = (void**)&current;
        ret = mem_unref(current_block);
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

void event_init(event_t* event, size_t size, event_direction_t direction, event_type_t* type)
{
    event->next_event = NULL;
    event->return_queue = NULL;
    event->direction = direction;
    event->type = type;
    event->data.len = size;
}
