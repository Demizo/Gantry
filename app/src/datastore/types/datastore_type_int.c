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

/**
 * @brief Logger for module
 */
LOG_MODULE_REGISTER(datastore_type_int, CONFIG_DATASTORE_TYPES_LOG_LEVEL);

//**********************************************************
//* Local Definitions
//**********************************************************

//**********************************************************
//* Static Function Declarations
//**********************************************************

static bool validate(const struct datastore_item_const_metadata* item, data_value_t value);
static bool is_default(const struct datastore_item_const_metadata* item);
static void set(const struct datastore_item_const_metadata* item, data_value_t value);
static int get(const struct datastore_item_const_metadata* item, data_value_t* out_value);
static int release(data_value_t* value);
static int encode(zcbor_state_t* encoder, data_value_t value);
static int decode(zcbor_state_t* decoder, data_value_t* out_value);

//**********************************************************
//* Static Variable Definitions
//**********************************************************

//**********************************************************
//* Static Function Definitions
//**********************************************************

static bool validate(const struct datastore_item_const_metadata* item, data_value_t value)
{
    ASSERT(value.type == DATASTORE_ITEM_TYPE_INT, "Unexpected value type");
    const int new_value = value.data.int_value;
    return (
        (new_value >= item->constraints.int_constraints.min) && (new_value <= item->constraints.int_constraints.max));
}

static bool is_default(const struct datastore_item_const_metadata* item)
{
    int current_value = *(int*)item->value_ptr;
    int default_value = item->default_value.int_value;

    return current_value == default_value;
}

static void set(const struct datastore_item_const_metadata* item, data_value_t value)
{
    ASSERT(value.type == DATASTORE_ITEM_TYPE_INT, "Unexpected value type");
    *(int*)(item->value_ptr) = value.data.int_value;
}

static int get(const struct datastore_item_const_metadata* item, data_value_t* out_value)
{
    out_value->type = DATASTORE_ITEM_TYPE_INT;
    out_value->data.int_value = *(int*)item->value_ptr;
    return SUCCESS;
}

static int release(data_value_t* value)
{
    ASSERT(value->type == DATASTORE_ITEM_TYPE_INT, "Unexpected value type");
    // No action, nothing to free
    return SUCCESS;
}

static int encode(zcbor_state_t* encoder, data_value_t value)
{
    ASSERT(value.type == DATASTORE_ITEM_TYPE_INT, "Unexpected value type");

    if (!zcbor_int32_put(encoder, value.data.int_value))
    {
        return -ENOMEM;
    }

    return SUCCESS;
}

static int decode(zcbor_state_t* decoder, data_value_t* out_value)
{
    int32_t decoded_value;

    if (!zcbor_int32_decode(decoder, &decoded_value))
    {
        return -EBADMSG;
    }

    out_value->type = DATASTORE_ITEM_TYPE_INT;
    out_value->data.int_value = (int)decoded_value;

    return SUCCESS;
}

//**********************************************************
//* Public Function Definitions
//**********************************************************

const struct datastore_item_interface datastore_int_interface = {
    .validate = validate,
    .is_default = is_default,
    .set = set,
    .get = get,
    .release = release,
    .decode = decode,
    .encode = encode,
};
