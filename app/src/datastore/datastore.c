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

#include "datastore_storage.h"
#include "datastore_types.h"
#include "error.h"
#include "generated_datastore_items.h"

/**
 * @brief Logger for module
 */
LOG_MODULE_REGISTER(datastore, CONFIG_DATASTORE_LOG_LEVEL);

//**********************************************************
//* Local Definitions
//**********************************************************

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
        LOG_ERR("Insufficient permissions to write item (id: %d)", id);
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
        LOG_ERR("Insufficient permissions to read item (id: %d)", id);
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
