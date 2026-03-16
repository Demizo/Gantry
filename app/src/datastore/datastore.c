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
    // TODO: Load values from NVM storage
    return;
}

int datastore_set(enum datastore_item_id id, const void* value)
{
    int ret = SUCCESS;
    const struct datastore_item_const_metadata* item = &g_datastore_const_metadata[id];

    if (!item->interface->validate(item, value))
    {
        LOG_ERR("Failed to set item %d, invalid value", id);
        return -EINVAL;
    }

    // TODO: Call other validators

    item->interface->set(item, value);

    // TODO: Notify listeners

    return ret;
}

int datastore_get(enum datastore_item_id id, void** out_value)
{
    const struct datastore_item_const_metadata* item = &g_datastore_const_metadata[id];
    return item->interface->get(item, out_value);
}

int datastore_release(enum datastore_item_id id, void** value)
{
    const struct datastore_item_const_metadata* item = &g_datastore_const_metadata[id];
    return item->interface->release(item, value);
}