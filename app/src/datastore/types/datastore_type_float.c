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

/**
 * @brief Logger for module
 */
LOG_MODULE_REGISTER(datastore_type_float, CONFIG_DATASTORE_TYPES_LOG_LEVEL);

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
static void release(data_value_t* value);
static int encode(zcbor_state_t* encoder, data_value_t value);
static int decode(zcbor_state_t* decoder, data_value_t* out_value);
static int encode_constraints(zcbor_state_t* encoder, const struct datastore_item_const_metadata* item);

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
    return (
        (new_value >= item->constraints.float_constraints.min) &&
        (new_value <= item->constraints.float_constraints.max));
}

static bool is_default(const struct datastore_item_const_metadata* item)
{
    float current_value = *(float*)item->value_ptr;
    float default_value = item->default_value.float_value;

    return current_value == default_value;
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

static void release(data_value_t* value)
{
    ASSERT(value->type == DATASTORE_ITEM_TYPE_FLOAT, "Unexpected value type");
    // No action, nothing to free
}

static int encode(zcbor_state_t* encoder, data_value_t value)
{
    ASSERT(value.type == DATASTORE_ITEM_TYPE_FLOAT, "Unexpected value type");

    if (!zcbor_float32_put(encoder, value.data.float_value))
    {
        return -ENOMEM;
    }

    return SUCCESS;
}

static int decode(zcbor_state_t* decoder, data_value_t* out_value)
{
    float decoded_value;

    if (!zcbor_float32_decode(decoder, &decoded_value))
    {
        return -EBADMSG;
    }

    out_value->type = DATASTORE_ITEM_TYPE_FLOAT;
    out_value->data.float_value = decoded_value;

    return SUCCESS;
}

static int encode_constraints(zcbor_state_t* encoder, const struct datastore_item_const_metadata* item)
{
    if (!zcbor_map_start_encode(encoder, 2) || !zcbor_tstr_put_lit(encoder, "min") ||
        !zcbor_float32_put(encoder, item->constraints.float_constraints.min) || !zcbor_tstr_put_lit(encoder, "max") ||
        !zcbor_float32_put(encoder, item->constraints.float_constraints.max) || !zcbor_map_end_encode(encoder, 2))
    {
        return -ENOMEM;
    }

    return SUCCESS;
}

//**********************************************************
//* Public Function Definitions
//**********************************************************

const struct datastore_item_interface datastore_float_interface = {
    .validate = validate,
    .is_default = is_default,
    .set = set,
    .get = get,
    .release = release,
    .decode = decode,
    .encode = encode,
    .encode_constraints = encode_constraints,
};
