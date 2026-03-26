/**
 * @file datastore_type_string.c
 * @author Demizo (demizodemazo@gmail.com)
 * @brief String type for datastore items
 * @version 0.1
 * @date 2026-03-01
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "datastore_type_string.h"

#include <stdint.h>
#include <string.h>
#include <sys/errno.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "datastore_types.h"
#include "error.h"
#include "memory.h"
#include "zephyr/toolchain.h"

LOG_MODULE_REGISTER(datastore_type_string, CONFIG_DATASTORE_TYPES_LOG_LEVEL);

//**********************************************************
//* Local Definitions
//**********************************************************

//**********************************************************
//* Static Function Declarations
//**********************************************************

static bool validate(const struct datastore_item_const_metadata* item, data_value_t value);
static void set(const struct datastore_item_const_metadata* item, data_value_t value);
static int get(const struct datastore_item_const_metadata* item, data_value_t* out_value);
static int release(const struct datastore_item_const_metadata* item, data_value_t* value);

//**********************************************************
//* Static Variable Definitions
//**********************************************************

//**********************************************************
//* Static Function Definitions
//**********************************************************

static bool validate(const struct datastore_item_const_metadata* item, data_value_t value)
{
    ASSERT(value.type == DATASTORE_ITEM_TYPE_STRING, "Unexpected value type");
    uint16_t len = strnlen(value.data.string_value, item->type_info.string_info.max_len + 1);
    // The string buffer is one byte larger than max length so that there is room for the null terminator
    return len <= item->type_info.string_info.max_len;
}

static void set(const struct datastore_item_const_metadata* item, data_value_t value)
{
    ASSERT(value.type == DATASTORE_ITEM_TYPE_STRING, "Unexpected value type");
    strncpy((char*)item->value_ptr, value.data.string_value, item->type_info.string_info.max_len);
    ((char*)(item->value_ptr))[item->type_info.string_info.max_len] = '\0';
}

static int get(const struct datastore_item_const_metadata* item, data_value_t* out_value)
{
    int ret = SUCCESS;
    uint16_t len = strnlen((const char*)item->value_ptr, item->type_info.string_info.max_len);

    void* string_block = NULL;
    ret = MEM_ALLOC(len, &string_block);
    if (ret == SUCCESS)
    {
        strncpy((char*)string_block, (const char*)item->value_ptr, len);
        ((char*)string_block)[len] = '\0';

        out_value->type = DATASTORE_ITEM_TYPE_STRING;
        out_value->data.string_value = string_block;
    }

    PASS_OWNERSHIP(string_block);
    return ret;
}

static int release(const struct datastore_item_const_metadata* item, data_value_t* value)
{
    ARG_UNUSED(item);
    ASSERT(value->type == DATASTORE_ITEM_TYPE_STRING, "Unexpected value type");

    void* string_block = value->data.string_value;
    return MEM_UNREF(&string_block);
}

//**********************************************************
//* Public Function Definitions
//**********************************************************

const struct datastore_item_interface datastore_string_interface = {
    .validate = validate,
    .set = set,
    .get = get,
    .release = release,
};
