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

#include <gantry/error.h>
#include <gantry/memory.h>
#include <gantry/stow/stow.h>
#include <gantry/stow/stow_event.h>
#include <gantry/stow/stow_storage.h>
#include <gantry/stow/types/stow_types.h>
#include <generated_stow_items.h>
#include <sys/errno.h>
#include <zephyr/init.h>
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
static void mark_subscriber_mode(enum stow_subscription_mode mode, bool* has_handle, bool* has_copy);
static void notify_one_subscriber(
    const struct stow_subscription* subscription, event_t* handle_event, event_t* copy_event);
static int stow_sys_init(void);

//**********************************************************
//* Static Variable Definitions
//**********************************************************

/**
 * @brief Initialization entry for the Stow
 */
SYS_INIT(stow_sys_init, POST_KERNEL, CONFIG_STOW_INIT_PRIORITY);

static struct stow_item_dynamic_metadata g_stow_dynamic_metadata[STOW_ID_COUNT] = { 0 };

//**********************************************************
//* Static Function Definitions
//**********************************************************

/**
 * @brief Track which subscription modes are present among a set of subscribers
 *
 * @param mode The subscription mode to check
 * @param[in,out] has_handle Set to true if @p mode is @ref STOW_SUBSCRIPTION_HANDLE
 * @param[in,out] has_copy Set to true if @p mode is @ref STOW_SUBSCRIPTION_COPY
 */
static void mark_subscriber_mode(enum stow_subscription_mode mode, bool* has_handle, bool* has_copy)
{
    if (mode == STOW_SUBSCRIPTION_HANDLE)
    {
        *has_handle = true;
    }
    else if (mode == STOW_SUBSCRIPTION_COPY)
    {
        *has_copy = true;
    }
}

/**
 * @brief Invoke a single subscriber's callback with the appropriate event
 *
 * @param subscription The subscription to notify
 * @param handle_event The handle-mode event, may be NULL if there are no handle subscribers
 * @param copy_event The copy-mode event, may be NULL if there are no copy subscribers
 */
static void notify_one_subscriber(
    const struct stow_subscription* subscription, event_t* handle_event, event_t* copy_event)
{
    // cppcheck-suppress ctuuninitvar
    if (subscription->cb == NULL)
    {
        return;
    }

    if (subscription->mode == STOW_SUBSCRIPTION_HANDLE)
    {
        subscription->cb(handle_event);
    }
    else if (subscription->mode == STOW_SUBSCRIPTION_COPY)
    {
        subscription->cb(copy_event);
    }
}

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
        mark_subscriber_mode(sub->subscription.mode, &has_handle_subscribers, &has_copy_subscribers);
    }

    STRUCT_SECTION_FOREACH(stow_static_subscription, static_sub)
    {
        if ((uint32_t)static_sub->id == item->id)
        {
            mark_subscriber_mode(static_sub->subscription.mode, &has_handle_subscribers, &has_copy_subscribers);
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
        if (item->custom_interface == NULL || item->custom_interface->get == NULL)
        {
            // Default getter
            ret = item->interface->get(item->value_ptr, &current_value);
        }
        else
        {
            // Custom getter
            ret = item->custom_interface->get(item, &current_value);
        }

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
        // cppcheck-suppress uninitvar
        notify_one_subscriber(&sub->subscription, handle_event, copy_event);
    }

    STRUCT_SECTION_FOREACH(stow_static_subscription, static_sub)
    {
        if ((uint32_t)static_sub->id == item->id)
        {
            notify_one_subscriber(&static_sub->subscription, handle_event, copy_event);
        }
    }

    // Release the initial reference. Subscribers claim ownership of events they are passed.
    EVENT_UNREF(&handle_event);
    EVENT_UNREF(&copy_event);
}

/**
 * @brief SYS_INIT entry point that runs @ref stow_init at boot
 *
 * @return SUCCESS always, to satisfy the SYS_INIT signature
 */
static int stow_sys_init(void)
{
    stow_init();
    return SUCCESS;
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

    // Validate static subscriptions registered via STOW_SUBSCRIPTION_DEFINE
    STRUCT_SECTION_FOREACH(stow_static_subscription, static_sub)
    {
        if (!stow_is_id_valid((uint32_t)static_sub->id))
        {
            LOG_ERR("Static subscription %p has invalid item id: %u", (void*)static_sub, (unsigned int)static_sub->id);
        }
    }

    ret = stow_storage_load();
    if (ret != SUCCESS)
    {
        LOG_ERR("Failed to load items from storage (%d)", ret);
    }

    return;
}

bool stow_is_id_valid(uint32_t id) { return id < STOW_ID_COUNT; }

int stow_set(stow_role_t current_auth, enum stow_item_id id, data_value_t value)
{
    int ret = SUCCESS;
    const struct stow_item_const_metadata* item = &g_stow_const_metadata[id];

    // Check permissions
    if (current_auth != STOW_ROLE_INTERNAL && (item->permissions.write_permissions & current_auth) == 0)
    {
        LOG_ERR("Insufficient permissions to write item id: %d", id);
        return -EACCES;
    }

    if (item->storage_type == STOW_STORAGE_TOFU)
    {
        // Disallow changing TOFU values after they are first modified
        data_value_t default_value = { .type = item->type, .data = item->default_value };
        data_value_t current_value = { 0 };
        if (item->custom_interface == NULL || item->custom_interface->get == NULL)
        {
            // Default getter
            ret = item->interface->get(item->value_ptr, &current_value);
        }
        else
        {
            // Custom getter
            ret = item->custom_interface->get(item, &current_value);
        }

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

    // Optional additional validation from the application
    if (item->custom_interface != NULL && item->custom_interface->validate != NULL)
    {
        if (!item->custom_interface->validate(item, value))
        {
            LOG_ERR("Failed custom validation for item %d", id);
            return -EINVAL;
        }
    }

    uint32_t key = irq_lock();

    // Set new value
    if (item->custom_interface == NULL || item->custom_interface->set == NULL)
    {
        // Default setter
        item->interface->set(item->value_ptr, value);
    }
    else
    {
        // Custom setter
        ret = item->custom_interface->set(item, value);
        if (ret != SUCCESS)
        {
            irq_unlock(key);
            LOG_ERR("Custom setter failed for item %d (%d)", id, ret);
            return ret;
        }
    }

    // Save to persistent storage, if relevant
    if ((item->storage_type == STOW_STORAGE_PERSISTENT) || item->storage_type == STOW_STORAGE_TOFU)
    {
        (void)stow_storage_save_item(item);
    }

    notify_subscribers(item);

    irq_unlock(key);

    return ret;
}

int stow_get(stow_role_t current_auth, enum stow_item_id id, data_value_t* out_value)
{
    int ret = SUCCESS;
    const struct stow_item_const_metadata* item = &g_stow_const_metadata[id];

    // Check permissions
    if (current_auth != STOW_ROLE_INTERNAL && (item->permissions.read_permissions & current_auth) == 0)
    {
        LOG_ERR("Insufficient permissions to read item id: %d", id);
        return -EACCES;
    }

    // Get current value
    uint32_t key = irq_lock();
    if (item->custom_interface == NULL || item->custom_interface->get == NULL)
    {
        // Default getter
        ret = item->interface->get(item->value_ptr, out_value);
    }
    else
    {
        // Custom getter
        ret = item->custom_interface->get(item, out_value);
    }
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

int stow_subscribe(stow_role_t current_auth, enum stow_item_id id, struct stow_subscription* subscription)
{
    int ret = SUCCESS;
    const struct stow_item_const_metadata* item = &g_stow_const_metadata[id];

    // Check permissions
    if (current_auth != STOW_ROLE_INTERNAL && (item->permissions.read_permissions & current_auth) == 0)
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
