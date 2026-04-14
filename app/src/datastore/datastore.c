/**
 * @file datastore.c
 * @author Demizo (demizodemazo@gmail.com)
 * @brief Core internal datastore interface
 * @version 0.1
 * @date 2026-03-01
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "datastore.h"

#include <sys/errno.h>
#include <zephyr/sys/slist.h>

#include "datastore_storage.h"
#include "datastore_types.h"
#include "error.h"
#include "generated_datastore_items.h"
#include "memory.h"

/**
 * @brief Logger for module
 */
LOG_MODULE_REGISTER(datastore, CONFIG_DATASTORE_LOG_LEVEL);

//**********************************************************
//* Local Definitions
//**********************************************************

/**
 * @brief Internal subscriber node (used to link multiple subscriptions)
 */
struct datastore_subscriber_node
{
    sys_snode_t node;                           /**< Linked list node */
    struct datastore_subscription subscription; /**< The datastore subscription */
};

//**********************************************************
//* Static Function Declarations
//**********************************************************

//**********************************************************
//* Static Variable Definitions
//**********************************************************

static struct datastore_item_dynamic_metadata g_datastore_dynamic_metadata[DATASTORE_ID_COUNT] = { 0 };

//**********************************************************
//* Static Function Definitions
//**********************************************************

//**********************************************************
//* Public Function Definitions
//**********************************************************

void datastore_init(void)
{
    int ret = SUCCESS;
    // Initialize default values
    for (int i = 0; i < DATASTORE_ID_COUNT; i++)
    {
        const struct datastore_item_const_metadata* item = &g_datastore_const_metadata[i];
        data_value_t value = {
            .type = item->type,
            .data = item->default_value,
        };
        TRACE_WRAP_VOID(item->interface->set(item, value));
    }

    // Initialize dynamic metadata
    for (int i = 0; i < DATASTORE_ID_COUNT; i++)
    {
        sys_slist_init(&g_datastore_dynamic_metadata[i].subscribers);
    }

    ret = datastore_storage_load();
    if (ret != SUCCESS)
    {
        LOG_ERR("Failed to load items from storage (%d)", ret);
    }

    return;
}

bool datastore_is_id_valid(uint32_t id) { return id < DATASTORE_ID_COUNT; }

int datastore_set(enum datastore_auth_level current_auth, enum datastore_item_id id, data_value_t value)
{
    int ret = SUCCESS;
    const struct datastore_item_const_metadata* item = &g_datastore_const_metadata[id];

    // Check permissions
    if (item->permissions.write_permissions > current_auth)
    {
        LOG_ERR("Insufficient permissions to write item id: %d", id);
        return -EACCES;
    }

    // Disallow changing TOFU values after they are first modified
    if ((item->storage_type == DATASTORE_STORAGE_TOFU) && !item->interface->is_default(item))
    {
        LOG_WRN("Attempted to change TOFU value that has already been modified, item id: %d", id);
        return -EACCES;
    }

    // Validate value
    if (!item->interface->validate(item, value))
    {
        LOG_ERR("Failed to set item %d, invalid value", id);
        return -EINVAL;
    }

    // TODO: Call other validators

    // Apply new value
    item->interface->set(item, value);

    // Save to persistent storage, if relevant
    if ((item->storage_type == DATASTORE_STORAGE_PERSISTENT) || item->storage_type == DATASTORE_STORAGE_TOFU)
    {
        (void)datastore_storage_save_item(item);
    }

    // TODO: Notify listeners

    return ret;
}

int datastore_get(enum datastore_auth_level current_auth, enum datastore_item_id id, data_value_t* out_value)
{
    const struct datastore_item_const_metadata* item = &g_datastore_const_metadata[id];

    // Check permissions
    if (item->permissions.read_permissions > current_auth)
    {
        LOG_ERR("Insufficient permissions to read item id: %d", id);
        return -EACCES;
    }

    // Get current value
    return item->interface->get(item, out_value);
}

void datastore_release(enum datastore_item_id id, data_value_t* value)
{
    const struct datastore_item_const_metadata* item = &g_datastore_const_metadata[id];
    item->interface->release(value);
}

int datastore_encode(zcbor_state_t* encoder, enum datastore_item_id id, data_value_t value)
{
    const struct datastore_item_const_metadata* item = &g_datastore_const_metadata[id];
    return item->interface->encode(encoder, value);
}

int datastore_decode(zcbor_state_t* decoder, enum datastore_item_id id, data_value_t* out_value)
{
    const struct datastore_item_const_metadata* item = &g_datastore_const_metadata[id];
    return item->interface->decode(decoder, out_value);
}

int datastore_subscribe(
    enum datastore_auth_level current_auth, enum datastore_item_id id, struct datastore_subscription* subscription)
{
    int ret = SUCCESS;
    const struct datastore_item_const_metadata* item = &g_datastore_const_metadata[id];

    // Check permissions
    if (item->permissions.read_permissions > current_auth)
    {
        LOG_ERR("Insufficient permissions to subscribe to item id: %d", id);
        return -EACCES;
    }

    // Ensure the subscription is not already in the list to prevent duplicates
    bool already_subscribed = false;
    struct datastore_subscriber_node* current_node;
    SYS_SLIST_FOR_EACH_CONTAINER(&g_datastore_dynamic_metadata[id].subscribers, current_node, node)
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
        return -EALREADY;
    }

    // Create subscriber node
    void* current_node_block = NULL;
    ret = MEM_ALLOC(sizeof(struct datastore_subscriber_node), &current_node_block);
    if (ret != SUCCESS)
    {
        LOG_ERR("Failed to allocate subscription node for item id: %d (%d)", id, ret);
        NOT_REFERENCED(current_node_block);
        return -ENOMEM;
    }

    current_node = (struct datastore_subscriber_node*)current_node_block;
    current_node->subscription = *subscription;

    uint32_t key = irq_lock();
    sys_slist_append(&g_datastore_dynamic_metadata[id].subscribers, &current_node->node);
    irq_unlock(key);

    LOG_DBG("Subscribed to item id: %d, mode: %d", id, subscription->mode);

    PASS_OWNERSHIP(current_node_block);
    return SUCCESS;
}

int datastore_unsubscribe(enum datastore_item_id id, struct datastore_subscription* subscription)
{
    struct datastore_subscriber_node* current_node = NULL;
    struct datastore_subscriber_node* matching_node = NULL;

    uint32_t key = irq_lock();

    // Find and remove the node for the provided subscription
    SYS_SLIST_FOR_EACH_CONTAINER(&g_datastore_dynamic_metadata[id].subscribers, current_node, node)
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
        sys_slist_find_and_remove(&g_datastore_dynamic_metadata[id].subscribers, &current_node->node),
        "Failed to remove subscription");

    irq_unlock(key);

    // Free the subscription node
    void* matching_node_block = (void*)matching_node;
    MEM_UNREF(&matching_node_block);

    LOG_DBG("Unsubscribed from item id: %d (%s)", id, g_datastore_const_metadata[id].name);
    return SUCCESS;
}
