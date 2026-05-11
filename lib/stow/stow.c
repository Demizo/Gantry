/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @author Demizo (demizodemazo@gmail.com)
 * @brief Core internal stow interface
 *
 *
 */

#include <generated_stow_items.h>
#include <sys/errno.h>
#include <zds/error.h>
#include <zds/memory.h>
#include <zds/stow/stow.h>
#include <zds/stow/stow_event.h>
#include <zds/stow/stow_storage.h>
#include <zds/stow/types/stow_types.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/slist.h>

/**
 * @brief Logger for module
 */
LOG_MODULE_REGISTER(stow, CONFIG_STOW_LOG_LEVEL);

//**********************************************************
//* Local Definitions
//**********************************************************

/**
 * @brief Internal subscriber node (used to link multiple subscriptions)
 */
struct stow_subscriber_node
{
    sys_snode_t node;                      /**< Linked list node */
    struct stow_subscription subscription; /**< The stow subscription */
};

//**********************************************************
//* Static Function Declarations
//**********************************************************

void notify_subscribers(const struct stow_item_const_metadata* item);

//**********************************************************
//* Static Variable Definitions
//**********************************************************

static struct stow_item_dynamic_metadata g_stow_dynamic_metadata[STOW_ID_COUNT] = { 0 };

//**********************************************************
//* Static Function Definitions
//**********************************************************

/**
 * @brief Notify subscribers that a given item has changed
 *
 * @details Calls the callback for each subscriber of a given item.
 *
 * @param item The item to send notifications for
 */
void notify_subscribers(const struct stow_item_const_metadata* item)
{
    int ret;
    bool has_handle_subscribers = false;
    event_t* handle_event = NULL;
    bool has_copy_subscribers = false;
    event_t* copy_event = NULL;
    struct stow_subscriber_node* sub;

    // Check subscriber types
    SYS_SLIST_FOR_EACH_CONTAINER(&g_stow_dynamic_metadata[item->id].subscribers, sub, node)
    {
        if (sub->subscription.mode == STOW_SUBSCRIPTION_HANDLE)
        {
            has_handle_subscribers = true;
        }
        else if (sub->subscription.mode == STOW_SUBSCRIPTION_COPY)
        {
            has_copy_subscribers = true;
        }
    }

    // Create handle update event if needed
    if (has_handle_subscribers)
    {
        ret = EVENT_ALLOC(&stow_update_event, sizeof(struct stow_update_event_payload), &handle_event);
        if (ret != SUCCESS)
        {
            LOG_ERR("Failed to allocate handle event for item %d (%d)", item->id, ret);
            NOT_REFERENCED(handle_event);
            return;
        }

        struct stow_update_event_payload* update_payload = (struct stow_update_event_payload*)handle_event->data.buf;

        update_payload->metadata = item;
        update_payload->mode = STOW_SUBSCRIPTION_HANDLE;
        update_payload->value_copy = (data_value_t){ 0 };
    }

    // Allocate copy update event if needed
    if (has_copy_subscribers)
    {
        ret = EVENT_ALLOC(&stow_update_event, sizeof(struct stow_update_event_payload), &copy_event);
        if (ret != SUCCESS)
        {
            LOG_ERR("Failed to allocate copy event for item %d (%d)", item->id, ret);
            EVENT_UNREF(&handle_event);
            NOT_REFERENCED(copy_event);
            return;
        }

        struct stow_update_event_payload* update_payload = (struct stow_update_event_payload*)copy_event->data.buf;

        update_payload->metadata = item;
        update_payload->mode = STOW_SUBSCRIPTION_COPY;

        // Set value copy to current value
        data_value_t current_value = { 0 };
        ret = item->interface->get(item->value_ptr, &current_value);
        if (ret != SUCCESS)
        {
            LOG_ERR("Failed to get current item value (%d)", ret);
            EVENT_UNREF(&handle_event);
            EVENT_UNREF(&copy_event);
            return;
        }
        update_payload->value_copy = current_value;
    }

    // Notify subscribers
    SYS_SLIST_FOR_EACH_CONTAINER(&g_stow_dynamic_metadata[item->id].subscribers, sub, node)
    {
        if (sub->subscription.cb != NULL)
        {
            if (sub->subscription.mode == STOW_SUBSCRIPTION_HANDLE)
            {
                EVENT_REF(handle_event);
                sub->subscription.cb(handle_event);
            }
            else if (sub->subscription.mode == STOW_SUBSCRIPTION_COPY)
            {
                EVENT_REF(copy_event);
                sub->subscription.cb(copy_event);
            }
        }
    }

    // Release the initial references. The reference count has been incremented for each subscriber. The
    // subscriber is responsible for dereferencing the event when complete.
    EVENT_UNREF(&handle_event);
    EVENT_UNREF(&copy_event);
}

//**********************************************************
//* Public Function Definitions
//**********************************************************

void stow_init(void)
{
    int ret = SUCCESS;
    // Initialize default values
    for (int i = 0; i < STOW_ID_COUNT; i++)
    {
        const struct stow_item_const_metadata* item = &g_stow_const_metadata[i];
        data_value_t value = {
            .type = item->type,
            .data = item->default_value,
        };
        TRACE_WRAP_VOID(item->interface->set(item->value_ptr, value));
    }

    // Initialize dynamic metadata
    for (int i = 0; i < STOW_ID_COUNT; i++)
    {
        sys_slist_init(&g_stow_dynamic_metadata[i].subscribers);
    }

    ret = stow_storage_load();
    if (ret != SUCCESS)
    {
        LOG_ERR("Failed to load items from storage (%d)", ret);
    }

    return;
}

bool stow_is_id_valid(uint32_t id) { return id < STOW_ID_COUNT; }

int stow_set(enum stow_auth_level current_auth, enum stow_item_id id, data_value_t value)
{
    int ret = SUCCESS;
    const struct stow_item_const_metadata* item = &g_stow_const_metadata[id];

    // Check permissions
    if (item->permissions.write_permissions > current_auth)
    {
        LOG_ERR("Insufficient permissions to write item id: %d", id);
        return -EACCES;
    }

    if (item->storage_type == STOW_STORAGE_TOFU)
    {
        // Disallow changing TOFU values after they are first modified
        data_value_t default_value = { .type = item->type, .data = item->default_value };
        data_value_t current_value = { 0 };
        ret = item->interface->get(item->value_ptr, &current_value);
        if (ret != SUCCESS)
        {
            LOG_ERR("Failed to check item %d against current value: %d", id, ret);
            return ret;
        }
        bool is_default = item->interface->is_equal(current_value, default_value);
        item->interface->release(&current_value);

        if (!is_default)
        {
            LOG_WRN("Attempted to change TOFU value that has already been modified, item id: %d", id);
            return -EACCES;
        }
    }

    // Validate value
    if (!item->interface->validate(&item->constraints, value))
    {
        LOG_ERR("Failed to set item %d, invalid value", id);
        return -EINVAL;
    }

    // TODO: Call other validators

    uint32_t key = irq_lock();

    // Apply new value
    item->interface->set(item->value_ptr, value);

    // Save to persistent storage, if relevant
    if ((item->storage_type == STOW_STORAGE_PERSISTENT) || item->storage_type == STOW_STORAGE_TOFU)
    {
        (void)stow_storage_save_item(item);
    }

    notify_subscribers(item);

    irq_unlock(key);

    return ret;
}

int stow_get(enum stow_auth_level current_auth, enum stow_item_id id, data_value_t* out_value)
{
    int ret = SUCCESS;
    const struct stow_item_const_metadata* item = &g_stow_const_metadata[id];

    // Check permissions
    if (item->permissions.read_permissions > current_auth)
    {
        LOG_ERR("Insufficient permissions to read item id: %d", id);
        return -EACCES;
    }

    // Get current value
    uint32_t key = irq_lock();
    ret = item->interface->get(item->value_ptr, out_value);
    irq_unlock(key);

    return ret;
}

void stow_release(enum stow_item_id id, data_value_t* value)
{
    const struct stow_item_const_metadata* item = &g_stow_const_metadata[id];
    item->interface->release(value);
}

int stow_encode(zcbor_state_t* encoder, enum stow_item_id id, data_value_t value)
{
    const struct stow_item_const_metadata* item = &g_stow_const_metadata[id];
    return item->interface->encode(encoder, value);
}

int stow_decode(zcbor_state_t* decoder, enum stow_item_id id, data_value_t* out_value)
{
    const struct stow_item_const_metadata* item = &g_stow_const_metadata[id];
    return item->interface->decode(decoder, out_value);
}

int stow_subscribe(enum stow_auth_level current_auth, enum stow_item_id id, struct stow_subscription* subscription)
{
    int ret = SUCCESS;
    const struct stow_item_const_metadata* item = &g_stow_const_metadata[id];

    // Check permissions
    if (item->permissions.read_permissions > current_auth)
    {
        LOG_ERR("Insufficient permissions to subscribe to item id: %d", id);
        return -EACCES;
    }

    uint32_t key = irq_lock();

    // Ensure the subscription is not already in the list to prevent duplicates
    bool already_subscribed = false;
    struct stow_subscriber_node* current_node;
    SYS_SLIST_FOR_EACH_CONTAINER(&g_stow_dynamic_metadata[id].subscribers, current_node, node)
    {
        if (current_node->subscription.cb == subscription->cb && current_node->subscription.mode == subscription->mode)
        {
            already_subscribed = true;
            break;
        }
    }

    if (already_subscribed)
    {
        LOG_WRN("Subscription already exists for item id: %d", id);
        irq_unlock(key);
        return -EALREADY;
    }

    // Create subscriber node
    void* current_node_block = NULL;
    ret = MEM_ALLOC(sizeof(struct stow_subscriber_node), &current_node_block);
    if (ret != SUCCESS)
    {
        LOG_ERR("Failed to allocate subscription node for item id: %d (%d)", id, ret);
        NOT_REFERENCED(current_node_block);
        irq_unlock(key);
        return -ENOMEM;
    }

    current_node = (struct stow_subscriber_node*)current_node_block;
    current_node->subscription = *subscription;

    sys_slist_append(&g_stow_dynamic_metadata[id].subscribers, &current_node->node);

    irq_unlock(key);

    LOG_DBG("Subscribed to item id: %d, mode: %d", id, subscription->mode);

    PASS_OWNERSHIP(current_node_block);
    return SUCCESS;
}

int stow_unsubscribe(enum stow_item_id id, struct stow_subscription* subscription)
{
    struct stow_subscriber_node* current_node = NULL;
    struct stow_subscriber_node* matching_node = NULL;

    uint32_t key = irq_lock();

    // Find and remove the node for the provided subscription
    SYS_SLIST_FOR_EACH_CONTAINER(&g_stow_dynamic_metadata[id].subscribers, current_node, node)
    {
        // cppcheck-suppress nullPointer
        if (current_node->subscription.cb == subscription->cb && current_node->subscription.mode == subscription->mode)
        {
            matching_node = current_node;
            break;
        }
    }

    if (matching_node == NULL)
    {
        LOG_WRN("Subscription not found for item id: %d", id);
        irq_unlock(key);
        return -ENOENT;
    }

    ASSERT(
        sys_slist_find_and_remove(&g_stow_dynamic_metadata[id].subscribers, &current_node->node),
        "Failed to remove subscription");

    irq_unlock(key);

    // Free the subscription node
    void* matching_node_block = (void*)matching_node;
    MEM_UNREF(&matching_node_block);

    LOG_DBG("Unsubscribed from item id: %d (%s)", id, g_stow_const_metadata[id].name);
    return SUCCESS;
}
