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

/** @cond INTERNAL_HIDDEN */
#ifdef CONFIG_STOW_CALLBACK_TIME_ASSERT
#define CALL_WITHIN_TIME_BUDGET(_what, _call)                                                                 \
    do                                                                                                        \
    {                                                                                                         \
        uint32_t _start_cycles = k_cycle_get_32();                                                            \
        _call;                                                                                                \
        uint32_t _elapsed_us = k_cyc_to_us_ceil32(k_cycle_get_32() - _start_cycles);                          \
        ASSERT(                                                                                               \
            _elapsed_us <= CONFIG_STOW_CALLBACK_TIME_BUDGET_US, "%s ran for %u us, budget is %u us", (_what), \
            _elapsed_us, (unsigned int)CONFIG_STOW_CALLBACK_TIME_BUDGET_US);                                  \
    } while (0)
#else
#define CALL_WITHIN_TIME_BUDGET(_what, _call) \
    do                                        \
    {                                         \
        _call;                                \
    } while (0)
#endif
/** @endcond */

//**********************************************************
//* Static Function Declarations
//**********************************************************

static int get_item_value(const struct stow_item_const_metadata* item, data_value_t* out_value);
static int set_item_value(const struct stow_item_const_metadata* item, data_value_t value);
static int check_tofu_unmodified(const struct stow_item_const_metadata* item);
static void mark_subscriber_mode(struct stow_item_dynamic_metadata* metadata, enum stow_subscription_mode mode);
static void refresh_subscriber_modes(enum stow_item_id id);
static void notify_subscribers(const struct stow_item_const_metadata* item);
static void notify_one_subscriber(
    const struct stow_subscription* subscription, event_t* handle_event, event_t* copy_event);
static void stow_flush_work_handler(struct k_work* work);
static int stow_sys_init(void);

//**********************************************************
//* Static Variable Definitions
//**********************************************************

/**
 * @brief Initialization entry for the Stow
 */
SYS_INIT(stow_sys_init, POST_KERNEL, CONFIG_STOW_INIT_PRIORITY);

static struct stow_item_dynamic_metadata g_stow_dynamic_metadata[STOW_ID_COUNT] = { 0 };

/**
 * @brief Debounced flush of dirty items to persistent storage
 */
static K_WORK_DELAYABLE_DEFINE(g_stow_flush_work, stow_flush_work_handler);
static K_THREAD_STACK_DEFINE(g_stow_flush_stack, CONFIG_STOW_STORAGE_FLUSH_THREAD_STACK_SIZE);
static struct k_work_q g_stow_flush_queue;
static const struct k_work_queue_config g_stow_flush_queue_config = { .name = "stow_flush" };

//**********************************************************
//* Static Function Definitions
//**********************************************************

/**
 * @brief Read an item's current value
 *
 * @param[in] item The item to read
 * @param[out] out_value Pointer to be populated with the item's current value
 *
 * @return SUCCESS when the value was retrieved, otherwise the error code
 */
static int get_item_value(const struct stow_item_const_metadata* item, data_value_t* out_value)
{
    if (item->custom_interface == NULL || item->custom_interface->get == NULL)
    {
        return item->interface->get(item->value_ptr, out_value);
    }

    int ret;
    CALL_WITHIN_TIME_BUDGET("custom_get", ret = item->custom_interface->get(item, out_value));
    return ret;
}

/**
 * @brief Write an item's value
 *
 * @param item The item to write
 * @param value The value to write, already validated
 *
 * @return SUCCESS when the value was written, otherwise the error code
 */
static int set_item_value(const struct stow_item_const_metadata* item, data_value_t value)
{
    if (item->custom_interface == NULL || item->custom_interface->set == NULL)
    {
        item->interface->set(item->value_ptr, value);
        return SUCCESS;
    }

    int ret;
    CALL_WITHIN_TIME_BUDGET("custom_set", ret = item->custom_interface->set(item, value));
    return ret;
}

/**
 * @brief Check that a TOFU item is still at its default value
 *
 * @param item The TOFU item to check
 *
 * @return SUCCESS when the item has not been modified yet
 * @return -EACCES when the item has already been modified
 * @return the error code when the current value could not be read
 */
static int check_tofu_unmodified(const struct stow_item_const_metadata* item)
{
    data_value_t current_value = { 0 };
    int ret = get_item_value(item, &current_value);
    if (ret != SUCCESS)
    {
        LOG_ERR("Failed to check item %d against current value: %d", item->id, ret);
        return ret;
    }

    data_value_t default_value = { .type = item->type, .data = item->default_value };
    bool is_default = item->interface->is_equal(current_value, default_value);
    item->interface->release(&current_value);

    if (!is_default)
    {
        LOG_WRN("Attempted to change TOFU value that has already been modified, item id: %d", item->id);
        return -EACCES;
    }

    return SUCCESS;
}

/**
 * @brief Record that an item has a subscriber in the given mode
 *
 * @param metadata Dynamic metadata of the item being subscribed to
 * @param mode Mode of the subscription being added
 */
static void mark_subscriber_mode(struct stow_item_dynamic_metadata* metadata, enum stow_subscription_mode mode)
{
    metadata->has_handle_subscribers |= (mode == STOW_SUBSCRIPTION_HANDLE);
    metadata->has_copy_subscribers |= (mode == STOW_SUBSCRIPTION_COPY);
}

/**
 * @brief Recompute an item's cached subscription mode flags
 *
 * @param id Item ID whose flags to recompute
 */
static void refresh_subscriber_modes(enum stow_item_id id)
{
    struct stow_item_dynamic_metadata* metadata = &g_stow_dynamic_metadata[id];
    metadata->has_handle_subscribers = false;
    metadata->has_copy_subscribers = false;

    struct stow_subscriber_node* sub;
    SYS_SLIST_FOR_EACH_CONTAINER(&metadata->subscribers, sub, node)
    {
        mark_subscriber_mode(metadata, sub->subscription.mode);
    }

    STRUCT_SECTION_FOREACH(stow_static_subscription, static_sub)
    {
        if (static_sub->id == id)
        {
            mark_subscriber_mode(metadata, static_sub->subscription.mode);
        }
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
        CALL_WITHIN_TIME_BUDGET("handle callback", subscription->cb(handle_event));
    }
    else if (subscription->mode == STOW_SUBSCRIPTION_COPY)
    {
        CALL_WITHIN_TIME_BUDGET("copy callback", subscription->cb(copy_event));
    }
}

/**
 * @brief Notify subscribers that a given item has changed
 *
 * @details Calls the callback for each subscriber of a given item.
 *
 * @param item The item to send notifications for
 */
static void notify_subscribers(const struct stow_item_const_metadata* item)
{
    int ret;
    const struct stow_item_dynamic_metadata* metadata = &g_stow_dynamic_metadata[item->id];
    event_t* handle_event = NULL;
    event_t* copy_event = NULL;
    struct stow_subscriber_node* sub;

    // Create handle update event if needed
    if (metadata->has_handle_subscribers)
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
    if (metadata->has_copy_subscribers)
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
        ret = get_item_value(item, &current_value);
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
 * @brief Delayed work entry point that flushes dirty items to persistent storage
 *
 * @param work Unused
 */
static void stow_flush_work_handler(struct k_work* work)
{
    ARG_UNUSED(work);
    (void)stow_flush();
}

/**
 * @brief SYS_INIT entry point that starts the flush queue and runs @ref stow_init at boot
 *
 * @return SUCCESS always, to satisfy the SYS_INIT signature
 */
static int stow_sys_init(void)
{
    k_work_queue_start(
        &g_stow_flush_queue, g_stow_flush_stack, K_THREAD_STACK_SIZEOF(g_stow_flush_stack),
        CONFIG_STOW_STORAGE_FLUSH_THREAD_PRIORITY, &g_stow_flush_queue_config);

    stow_init();
    return SUCCESS;
}

//**********************************************************
//* Public Function Definitions
//**********************************************************

void stow_init(void)
{
    (void)k_work_cancel_delayable(&g_stow_flush_work);

    for (int i = 0; i < STOW_ID_COUNT; i++)
    {
        const struct stow_item_const_metadata* item = &g_stow_const_metadata[i];
        data_value_t value = {
            .type = item->type,
            .data = item->default_value,
        };
        TRACE_WRAP_VOID(item->interface->set(item->value_ptr, value));

        g_stow_dynamic_metadata[i] = (struct stow_item_dynamic_metadata){ 0 };
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

    // Compute initial subscription types
    for (int i = 0; i < STOW_ID_COUNT; i++)
    {
        refresh_subscriber_modes((enum stow_item_id)i);
    }

    int ret = stow_storage_load();
    if (ret != SUCCESS)
    {
        LOG_ERR("Failed to load items from storage (%d)", ret);
    }

    return;
}

bool stow_is_id_valid(uint32_t id) { return id < STOW_ID_COUNT; }

int stow_flush(void)
{
    int first_error = SUCCESS;

    for (int i = 0; i < STOW_ID_COUNT; i++)
    {
        //? Note: Dirty flag is cleared first so writes occurring during a save are picked up on the next flush
        uint32_t key = irq_lock();
        bool dirty = g_stow_dynamic_metadata[i].dirty;
        g_stow_dynamic_metadata[i].dirty = false;
        irq_unlock(key);

        if (!dirty)
        {
            continue;
        }

        int ret = stow_storage_save_item(&g_stow_const_metadata[i]);
        if (ret != SUCCESS)
        {
            LOG_ERR("Failed to save item %d (%d)", i, ret);
            if (first_error == SUCCESS)
            {
                first_error = ret;
            }
        }
    }

    return first_error;
}

int stow_set_external(stow_role_t current_auth, enum stow_item_id id, data_value_t value)
{
    const struct stow_item_const_metadata* item = &g_stow_const_metadata[id];

    if ((item->permissions.write_permissions & current_auth) == 0)
    {
        LOG_ERR("Insufficient permissions to write item id: %d", id);
        return -EACCES;
    }

    return stow_set(id, value);
}

int stow_set(enum stow_item_id id, data_value_t value)
{
    int ret = SUCCESS;
    const struct stow_item_const_metadata* item = &g_stow_const_metadata[id];

    uint32_t key = irq_lock();

    if (item->storage_type == STOW_STORAGE_TOFU)
    {
        ret = check_tofu_unmodified(item);
        if (ret != SUCCESS)
        {
            irq_unlock(key);
            return ret;
        }
    }

    if (!item->interface->validate(&item->constraints, value))
    {
        irq_unlock(key);
        LOG_ERR("Failed to set item %d, invalid value", id);
        return -EINVAL;
    }

    // Optional additional validation from the application
    if (item->custom_interface != NULL && item->custom_interface->validate != NULL)
    {
        bool accepted = false;
        CALL_WITHIN_TIME_BUDGET("custom_validate", accepted = item->custom_interface->validate(item, value));
        if (!accepted)
        {
            irq_unlock(key);
            LOG_ERR("Failed custom validation for item %d", id);
            return -EINVAL;
        }
    }

    ret = set_item_value(item, value);
    if (ret != SUCCESS)
    {
        irq_unlock(key);
        LOG_ERR("Setter failed for item %d (%d)", id, ret);
        return ret;
    }

    if ((item->storage_type == STOW_STORAGE_PERSISTENT) || (item->storage_type == STOW_STORAGE_TOFU))
    {
        g_stow_dynamic_metadata[id].dirty = true;

        //? Scheduled rather than rescheduled to prevent a stream of writes from pushing back the flush deadline.
        (void)k_work_schedule_for_queue(
            &g_stow_flush_queue, &g_stow_flush_work, K_MSEC(CONFIG_STOW_STORAGE_FLUSH_DELAY_MS));
    }

    notify_subscribers(item);

    irq_unlock(key);

    return SUCCESS;
}

int stow_get_external(stow_role_t current_auth, enum stow_item_id id, data_value_t* out_value)
{
    const struct stow_item_const_metadata* item = &g_stow_const_metadata[id];

    if ((item->permissions.read_permissions & current_auth) == 0)
    {
        LOG_ERR("Insufficient permissions to read item id: %d", id);
        return -EACCES;
    }

    return stow_get(id, out_value);
}

int stow_get(enum stow_item_id id, data_value_t* out_value)
{
    const struct stow_item_const_metadata* item = &g_stow_const_metadata[id];

    uint32_t key = irq_lock();
    int ret = get_item_value(item, out_value);
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

int stow_subscribe_external(stow_role_t current_auth, enum stow_item_id id, struct stow_subscription* subscription)
{
    const struct stow_item_const_metadata* item = &g_stow_const_metadata[id];

    if ((item->permissions.read_permissions & current_auth) == 0)
    {
        LOG_ERR("Insufficient permissions to subscribe to item id: %d", id);
        return -EACCES;
    }

    return stow_subscribe(id, subscription);
}

int stow_subscribe(enum stow_item_id id, struct stow_subscription* subscription)
{
    int ret = SUCCESS;

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
    mark_subscriber_mode(&g_stow_dynamic_metadata[id], subscription->mode);

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
    refresh_subscriber_modes(id);

    irq_unlock(key);

    // Free the subscription node
    void* matching_node_block = (void*)matching_node;
    MEM_UNREF(&matching_node_block);

    LOG_DBG("Unsubscribed from item id: %d (%s)", id, g_stow_const_metadata[id].name);
    return SUCCESS;
}
