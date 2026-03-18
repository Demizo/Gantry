/**
 * @file datastore_type_float.c
 * @author Demizo (demizodemazo@gmail.com)
 * @brief Float type for datastore items
 * @version 0.1
 * @date 2026-03-01
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "datastore_type_float.h"

#include <sys/errno.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "datastore_types.h"
#include "error.h"
#include "zephyr/toolchain.h"

LOG_MODULE_REGISTER(datastore_type_float, CONFIG_DATASTORE_TYPES_LOG_LEVEL);

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
    ASSERT(value.type == DATASTORE_ITEM_TYPE_FLOAT, "Unexpected value type");
    const float new_value = value.data.float_value;
    return ((new_value >= item->type_info.float_info.min) && (new_value <= item->type_info.float_info.max));
}

static void set(const struct datastore_item_const_metadata* item, data_value_t value)
{
    ASSERT(value.type == DATASTORE_ITEM_TYPE_FLOAT, "Unexpected value type");
    *(float*)(item->value_ptr) = value.data.float_value;
}

static int get(const struct datastore_item_const_metadata* item, data_value_t* out_value)
{
    out_value->type = DATASTORE_ITEM_TYPE_FLOAT;
    out_value->data.float_value = *(float*)item->value_ptr;
    return SUCCESS;
}

static int release(const struct datastore_item_const_metadata* item, data_value_t* value)
{
    ARG_UNUSED(item);
    ASSERT(value->type == DATASTORE_ITEM_TYPE_FLOAT, "Unexpected value type");
    // No action, nothing to free
    return SUCCESS;
}

//**********************************************************
//* Public Function Definitions
//**********************************************************

const struct datastore_item_interface datastore_float_interface = {
    .validate = validate,
    .set = set,
    .get = get,
    .release = release,
};
