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

static bool validate(const struct datastore_item_const_metadata* item, const void* value);
static void set(const struct datastore_item_const_metadata* item, const void* value);
static int get(const struct datastore_item_const_metadata* item, void** out_value);
static int release(const struct datastore_item_const_metadata* item, void** value);

//**********************************************************
//* Static Variable Definitions
//**********************************************************

//**********************************************************
//* Static Function Definitions
//**********************************************************

static bool validate(const struct datastore_item_const_metadata* item, const void* value)
{
    uint16_t len = strnlen((const char*)value, item->type_info.string_info.max_len);
    // The string must be less than the max length so that there is room for the null terminator
    return len < item->type_info.string_info.max_len;
}

static void set(const struct datastore_item_const_metadata* item, const void* value)
{
    strncpy((char*)item->value_ptr, (const char*)value, item->type_info.string_info.max_len);
    ((char*)(item->value_ptr))[item->type_info.string_info.max_len - 1] = '\0';
}

static int get(const struct datastore_item_const_metadata* item, void** out_value)
{
    int ret = SUCCESS;
    uint16_t len = strnlen((const char*)item->value_ptr, item->type_info.string_info.max_len);

    ret = MEM_ALLOC(len, out_value);
    if (ret == SUCCESS)
    {
        strncpy((char*)*out_value, (const char*)item->value_ptr, len);
        ((char*)*out_value)[len] = '\0';
    }

    PASS_OWNERSHIP(out_value);
    return ret;
}

static int release(const struct datastore_item_const_metadata* item, void** value)
{
    ARG_UNUSED(item);

    return MEM_UNREF(value);
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
