/**
 * @file datastore_type_int.c
 * @author Demizo (demizodemazo@gmail.com)
 * @brief Int type for datastore items
 * @version 0.1
 * @date 2026-03-01
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "datastore_type_int.h"

#include <sys/errno.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "datastore_types.h"
#include "error.h"
#include "zephyr/toolchain.h"

LOG_MODULE_REGISTER(datastore_type_int, CONFIG_DATASTORE_TYPES_LOG_LEVEL);

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
    const int new_value = *(const int*)value;
    return ((new_value >= item->type_info.int_info.min) && (new_value <= item->type_info.int_info.max));
}

static void set(const struct datastore_item_const_metadata* item, const void* value)
{
    *(int*)(item->value_ptr) = *(const int*)value;
}

static int get(const struct datastore_item_const_metadata* item, void** out_value)
{
    *(int*)out_value = *(int*)item->value_ptr;
    return SUCCESS;
}

static int release(const struct datastore_item_const_metadata* item, void** value)
{
    ARG_UNUSED(item);

    *value = NULL;
    return SUCCESS;
}

//**********************************************************
//* Public Function Definitions
//**********************************************************

const struct datastore_item_interface datastore_int_interface = {
    .validate = validate,
    .set = set,
    .get = get,
    .release = release,
};
