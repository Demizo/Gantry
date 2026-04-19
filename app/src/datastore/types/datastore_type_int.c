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

static bool validate(const union datastore_constraints* constraints, data_value_t value);
static bool is_equal(data_value_t a, data_value_t b);
static void set(void* dest, data_value_t value);
static int get(const struct datastore_item_const_metadata* item, data_value_t* out_value);
static void release(data_value_t* value);
static int encode(zcbor_state_t* encoder, data_value_t value);
static int decode(zcbor_state_t* decoder, data_value_t* out_value);
static int encode_constraints(zcbor_state_t* encoder, const union datastore_constraints* constraints);

//**********************************************************
//* Static Variable Definitions
//**********************************************************

//**********************************************************
//* Static Function Definitions
//**********************************************************

static bool validate(const union datastore_constraints* constraints, data_value_t value)
{
    ASSERT(value.type == DATASTORE_ITEM_TYPE_INT, "Unexpected value type");
    const int new_value = value.data.int_value;
    return ((new_value >= constraints->int_constraints.min) && (new_value <= constraints->int_constraints.max));
}

static bool is_equal(data_value_t a, data_value_t b)
{
    ASSERT((a.type == DATASTORE_ITEM_TYPE_INT) && (b.type == DATASTORE_ITEM_TYPE_INT), "Unexpected value type");

    return a.data.int_value == b.data.int_value;
}

static void set(void* dest, data_value_t value)
{
    ASSERT(value.type == DATASTORE_ITEM_TYPE_INT, "Unexpected value type");
    *(int*)dest = value.data.int_value;
}

static int get(const struct datastore_item_const_metadata* item, data_value_t* out_value)
{
    out_value->type = DATASTORE_ITEM_TYPE_INT;
    out_value->data.int_value = *(int*)item->value_ptr;
    return SUCCESS;
}

static void release(data_value_t* value)
{
    ASSERT(value->type == DATASTORE_ITEM_TYPE_INT, "Unexpected value type");
    // No action, nothing to free
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

static int encode_constraints(zcbor_state_t* encoder, const union datastore_constraints* constraints)
{
    if (!zcbor_map_start_encode(encoder, 2) || !zcbor_tstr_put_lit(encoder, "min") ||
        !zcbor_int32_put(encoder, constraints->int_constraints.min) || !zcbor_tstr_put_lit(encoder, "max") ||
        !zcbor_int32_put(encoder, constraints->int_constraints.max) || !zcbor_map_end_encode(encoder, 2))
    {
        return -ENOMEM;
    }

    return SUCCESS;
}

//**********************************************************
//* Public Function Definitions
//**********************************************************

const struct datastore_item_interface datastore_int_interface = {
    .validate = validate,
    .is_equal = is_equal,
    .set = set,
    .get = get,
    .release = release,
    .decode = decode,
    .encode = encode,
    .encode_constraints = encode_constraints,
};
