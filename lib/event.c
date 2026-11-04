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

#include <gantry/error.h>
#include <gantry/event.h>
#include <gantry/memory.h>
#include <string.h>
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

static void release_event(void* block);

//**********************************************************
//* Static Variable Definitions
//**********************************************************

//**********************************************************
//* Static Function Definitions
//**********************************************************

/**
 * @brief Call the event type's on_free, if it has one, before the event's block is freed
 *
 * @param block The event being freed
 */
static void release_event(void* block)
{
    event_t* event = block;
    if (event->type->on_free != NULL)
    {
        event->type->on_free(event);
    }
}

//**********************************************************
//* Public Function Definitions
//**********************************************************

int event_alloc(const event_type_t* type, size_t payload_size, event_t** event_ptr)
{
    int ret = SUCCESS;

    // Account for event header size in total size
    size_t total_size = sizeof(event_t) + payload_size;
    ret = mem_alloc(total_size, (void**)event_ptr);
    if (ret)
    {
        LOG_ERR("Failed to allocate event: %d", ret);
        return ret;
    }

    event_init(*event_ptr, type, payload_size);

    LOG_DBG("Event allocated: type: %d, payload size: %zu", type->id, payload_size);

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
    bool head_freed = false;

    // Iterate over linked events, decrementing the reference count
    current = *event;
    while (current != NULL && (chain_length < LINKED_EVENT_CHAIN_LEN_MAX))
    {
        // Get the linked event first in case the block is freed
        next = current->next_event;

        void** current_block = (void**)&current;
        mem_unref_with(current_block, release_event);

        if (chain_length == 0)
        {
            head_freed = (current == NULL);
        }

        current = next;

        chain_length++;
    }

    if (head_freed)
    {
        *event = NULL;
    }

    LOG_DBG("Decremented reference count for event");
}

void event_init(event_t* event, const event_type_t* type, size_t payload_size)
{
    event->next_event = NULL;
    event->type = type;
    event->data.len = payload_size;
}
