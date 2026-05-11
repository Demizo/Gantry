/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @author Demizo (demizodemazo@gmail.com)
 * @brief Event implementation
 *
 *
 */

#include <string.h>
#include <zds/error.h>
#include <zds/event.h>
#include <zds/memory.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

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

int event_alloc(const event_type_t* type, size_t payload_size, event_t** event_ptr)
{
    int ret = SUCCESS;

    // Account for event header size in total size
    size_t total_size = sizeof(event_t) + payload_size;
    void** event_block = (void**)event_ptr;
    ret = mem_alloc(total_size, event_block);
    if (ret)
    {
        LOG_ERR("Failed to allocate event: %d", ret);

        NOT_REFERENCED(event_block);
        return ret;
    }

    event_init(*event_ptr, type, payload_size);

    LOG_DBG("Event allocated: type: %d, payload size: %zu", type->id, payload_size);

    PASS_OWNERSHIP(event_block);
    return SUCCESS;
}

void event_ref(event_t* event)
{
    event_t* current = NULL;

    // Iterate over linked events, incrementing the reference count
    current = event;
    while (current != NULL)
    {
        mem_ref(current);
        current = current->next_event;
    }

    LOG_DBG("Incremented reference count for event");

    PASS_OWNERSHIP(current);
}

void event_unref(event_t** event)
{
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
        mem_unref(current_block);
        current = next;

        chain_length++;
    }

    LOG_DBG("Decremented reference count for event");
}

void event_init(event_t* event, const event_type_t* type, size_t payload_size)
{
    event->next_event = NULL;
    event->type = type;
    event->data.len = payload_size;
}
