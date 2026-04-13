/**
 * @file datastore_storage.c
 * @author Demizo (demizodemazo@gmail.com)
 * @brief Non-volatile storage backend for the datastore
 * @version 0.1
 * @date 2026-03-01
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "datastore_storage.h"

#include <string.h>
#include <sys/errno.h>

#include "datastore_types.h"
#include "error.h"
#include "generated_datastore_items.h"
#include "memory.h"
#include "zephyr/logging/log.h"
#include "zephyr/settings/settings.h"

/**
 * @brief Logger for module
 */
LOG_MODULE_REGISTER(datastore_storage, CONFIG_DATASTORE_STORAGE_LOG_LEVEL);

//**********************************************************
//* Local Definitions
//**********************************************************

/**
 * @brief Subtree prefix for datastore items
 */
#define DATASTORE_SETTINGS_SUBTREE "ds"

//**********************************************************
//* Static Function Declarations
//**********************************************************

static enum datastore_item_id get_id_from_name(const char* name);
static int delete_stored_value(const char* name);
static int datastore_set_handler(const char* name, size_t len, settings_read_cb read_cb, void* cb_arg);

//**********************************************************
//* Static Variable Definitions
//**********************************************************

/**
 * @brief Handler for the datastore settings subtree
 */
SETTINGS_STATIC_HANDLER_DEFINE(
    datastore_settings_handler, DATASTORE_SETTINGS_SUBTREE, NULL, datastore_set_handler, NULL, NULL);

//**********************************************************
//* Static Function Definitions
//**********************************************************

/**
 * @brief Get the item id from an item name
 *
 * @param name Name of the data item
 *
 * @return item ID when the name matches a datastore item
 * @return @ref DATASTORE_ITEM_TYPE_COUNT when the name does not match an item
 */
static enum datastore_item_id get_id_from_name(const char* name)
{
    enum datastore_item_id item_id = DATASTORE_ID_COUNT;

    for (int i = 0; i < DATASTORE_ID_COUNT; i++)
    {
        const struct datastore_item_const_metadata* item = &g_datastore_const_metadata[i];

        // SAFETY: Item names are guaranteed to be terminated
        if (strcmp(item->name, name) == 0)
        {
            item_id = (enum datastore_item_id)i;
            break;
        }
    }

    return item_id;
}

/**
 * @brief Delete a data item from storage
 *
 * @param name The name of the item to delete
 *
 * @return SUCCESS when the item is deleted, otherwise the error code
 */
static int delete_stored_value(const char* name)
{
    int ret = SUCCESS;

    // The length of the setting with the datastore prefix (+ 1 for '/', + 1 for null terminator)
    // SAFETY: Subtree and name strings are guaranteed to be terminated.
    uint16_t settings_name_len = strlen(DATASTORE_SETTINGS_SUBTREE) + 1 + strlen(name) + 1;
    void* settings_name = NULL;
    ret = MEM_ALLOC(settings_name_len, &settings_name);
    if (ret != SUCCESS)
    {
        LOG_ERR("Failed to allocate name during deletion of %s (%d)", name, ret);
        NOT_REFERENCED(settings_name);
        return ret;
    }

    int len = snprintf(settings_name, settings_name_len, "%s/%s", DATASTORE_SETTINGS_SUBTREE, name);
    ASSERT(len == (settings_name_len - 1), "Failed to construct settings key");

    ret = settings_delete(settings_name);
    MEM_UNREF(&settings_name);
    if (ret != SUCCESS)
    {
        LOG_ERR("Failed to delete %s", name);
    }
    return ret;
}

/**
 * @brief
 *
 * @param name Name of the data item
 * @param len Stored length of the data item's value
 * @param read_cb Callback to read the stored value
 * @param cb_arg Arguments to pass to the read_cb
 *
 * @return SUCCESS when the item was loaded, otherwise the error code
 */
static int datastore_set_handler(const char* name, size_t len, settings_read_cb read_cb, void* cb_arg)
{
    int ret = SUCCESS;
    enum datastore_item_id item_id = get_id_from_name(name);
    if (item_id == DATASTORE_ID_COUNT) return -EINVAL;

    const struct datastore_item_const_metadata* item = &g_datastore_const_metadata[item_id];

    void* read_block = NULL;
    ret = MEM_ALLOC(len, &read_block);
    if (ret != SUCCESS)
    {
        LOG_ERR("Failed to allocate block of size %d to load %s (%d)", len, name, ret);
        NOT_REFERENCED(read_block);
        return ret;
    }

    size_t read_len = read_cb(cb_arg, read_block, len);
    if (read_len != len)
    {
        LOG_ERR("Failed to read %s", name);
        MEM_UNREF(&read_block);
        (void)delete_stored_value(name);
        return ret;
    }

    ZCBOR_STATE_D(decoder, 1, read_block, len, 1, 0);
    data_value_t decoded_value = { 0 };
    ret = item->interface->decode(decoder, &decoded_value);
    MEM_UNREF(&read_block);
    if (ret != SUCCESS)
    {
        LOG_ERR("Failed to decode %s", name);
        (void)delete_stored_value(name);
        return ret;
    }

    if (!item->interface->validate(item, decoded_value))
    {
        LOG_ERR("Stored value for %s was not valid", name);
        item->interface->release(&decoded_value);
        (void)delete_stored_value(name);
        return -EINVAL;
    }

    item->interface->set(item, decoded_value);
    item->interface->release(&decoded_value);

    if (item->interface->is_default(item))
    {
        LOG_DBG("The stored value of %s matches the default value, deleting", name);
        (void)delete_stored_value(name);
    }

    LOG_DBG("Loaded %s from storage", name);

    return ret;
}

//**********************************************************
//* Public Function Definitions
//**********************************************************

int datastore_storage_load(void)
{
    int ret = SUCCESS;
    ret = settings_subsys_init();
    if (ret != SUCCESS)
    {
        LOG_ERR("Failed to initialize settings subsystem (%d)", ret);
        return ret;
    }

    ret = settings_load_subtree(DATASTORE_SETTINGS_SUBTREE);
    if (ret != SUCCESS)
    {
        LOG_ERR("Failed to load datastore subtree (%d)", ret);
    }

    return ret;
}

int datastore_storage_save_item(const struct datastore_item_const_metadata* item)
{
    int ret = SUCCESS;

    // The length of the setting with the datastore prefix (+ 1 for '/', + 1 for null terminator)
    // SAFETY: Subtree and name strings are guaranteed to be terminated.
    uint16_t settings_name_len = strlen(DATASTORE_SETTINGS_SUBTREE) + 1 + strlen(item->name) + 1;
    void* settings_name = NULL;
    ret = MEM_ALLOC(settings_name_len, &settings_name);
    if (ret != SUCCESS)
    {
        LOG_ERR("Failed to allocate name when saving %s (%d)", item->name, ret);
        NOT_REFERENCED(settings_name);
        return ret;
    }

    int len = snprintf(settings_name, settings_name_len, "%s/%s", DATASTORE_SETTINGS_SUBTREE, item->name);
    ASSERT(len == (settings_name_len - 1), "Failed to construct settings key");

    void* encoded_value_block = NULL;
    ret = MEM_ALLOC(CONFIG_MEM_LARGE_BLOCK_SIZE, &encoded_value_block);
    if (ret != SUCCESS)
    {
        LOG_ERR("Failed to allocate encoded value block when saving %s (%d)", item->name, ret);
        MEM_UNREF(&settings_name);
        NOT_REFERENCED(encoded_value_block);
        return ret;
    }

    ZCBOR_STATE_E(encoder, 1, encoded_value_block, CONFIG_MEM_LARGE_BLOCK_SIZE, 1);
    data_value_t current_value = { 0 };
    ret = item->interface->get(item, &current_value);
    if (ret != SUCCESS)
    {
        LOG_ERR("Failed to get current value when saving %s (%d)", item->name, ret);
        MEM_UNREF(&settings_name);
        MEM_UNREF(&encoded_value_block);
        return ret;
    }

    ret = item->interface->encode(encoder, current_value);
    item->interface->release(&current_value);
    if (ret != SUCCESS)
    {
        LOG_ERR("Failed to encode value when saving %s (%d)", item->name, ret);
        MEM_UNREF(&settings_name);
        MEM_UNREF(&encoded_value_block);
        return ret;
    }

    ret = settings_save_one(settings_name, encoded_value_block, (encoder->payload - (uint8_t*)encoded_value_block));
    MEM_UNREF(&settings_name);
    MEM_UNREF(&encoded_value_block);
    if (ret != SUCCESS)
    {
        LOG_ERR("Failed to save %s (%d)", item->name, ret);
    }
    return ret;
}