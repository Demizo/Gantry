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

#include "datastore_types.h"
#include "error.h"
#include "generated_datastore_items.h"

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
    // Initialize default values
    for (int i = 0; i < DATASTORE_ID_COUNT; i++)
    {
        const struct datastore_item_const_metadata* item = &g_datastore_const_metadata[i];
        data_value_t value = {
            .type = item->type,
            .data = item->default_value,
        };
        item->interface->set(item, value);
    }

    // TODO: Load values from NVM storage
    return;
}

int datastore_set(enum datastore_auth_level current_auth, enum datastore_item_id id, data_value_t value)
{
    int ret = SUCCESS;

    // Validate ID
    if ((id < 0) || (id >= DATASTORE_ID_COUNT))
    {
        LOG_ERR("Invalid item id (%d)", id);
        return -EINVAL;
    }
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
        LOG_ERR("TOFU value has already been modified, item (id: %d)", id);
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

    // TODO: Notify listeners

    return ret;
}

int datastore_get(enum datastore_auth_level current_auth, enum datastore_item_id id, data_value_t* out_value)
{
    // Validate ID
    if ((id < 0) || (id >= DATASTORE_ID_COUNT))
    {
        LOG_ERR("Invalid item id (%d)", id);
        return -EINVAL;
    }
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

int datastore_release(enum datastore_item_id id, data_value_t* value)
{
    const struct datastore_item_const_metadata* item = &g_datastore_const_metadata[id];
    return item->interface->release(item, value);
}
