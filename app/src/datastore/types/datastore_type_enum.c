/**
 * @file datastore_type_enum.c
 * @author Demizo (demizodemazo@gmail.com)
 * @brief Enum type for datastore items
 * @version 0.1
 * @date 2026-03-01
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "datastore_type_enum.h"

#include <sys/errno.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "datastore_types.h"
#include "error.h"

/**
 * @brief Logger for module
 */
LOG_MODULE_REGISTER(datastore_type_enum, CONFIG_DATASTORE_TYPES_LOG_LEVEL);

//**********************************************************
//* Local Definitions
//**********************************************************

//**********************************************************
//* Static Function Declarations
//**********************************************************

static bool validate(const union datastore_constraints* constraints, data_value_t value);
static bool is_equal(data_value_t a, data_value_t b);
static void set(void* dest, data_value_t value);
static int get(void* src, data_value_t* out_value);
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
    ASSERT(value.type == DATASTORE_ITEM_TYPE_ENUM, "Unexpected value type");
    const int new_value = value.data.int_value;

    char* name = NULL;
    return (enum_get_name_from_value(constraints, new_value, &name) == SUCCESS);
}

static bool is_equal(data_value_t a, data_value_t b)
{
    ASSERT((a.type == DATASTORE_ITEM_TYPE_ENUM) && (b.type == DATASTORE_ITEM_TYPE_ENUM), "Unexpected value type");

    return a.data.int_value == b.data.int_value;
}

static void set(void* dest, data_value_t value)
{
    ASSERT(value.type == DATASTORE_ITEM_TYPE_ENUM, "Unexpected value type");
    *(int*)dest = value.data.int_value;
}

static int get(void* src, data_value_t* out_value)
{
    out_value->type = DATASTORE_ITEM_TYPE_ENUM;
    out_value->data.int_value = *(int*)src;
    return SUCCESS;
}

static void release(data_value_t* value)
{
    ASSERT(value->type == DATASTORE_ITEM_TYPE_ENUM, "Unexpected value type");
    // No action, nothing to free
}

static int encode(zcbor_state_t* encoder, data_value_t value)
{
    ASSERT(value.type == DATASTORE_ITEM_TYPE_ENUM, "Unexpected value type");

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

    out_value->type = DATASTORE_ITEM_TYPE_ENUM;
    out_value->data.int_value = (int)decoded_value;

    return SUCCESS;
}

static int encode_constraints(zcbor_state_t* encoder, const union datastore_constraints* constraints)
{
    uint16_t count = constraints->enum_constraints.value_count;
    const struct data_enum_value* values = constraints->enum_constraints.values;

    if (!zcbor_list_start_encode(encoder, count))
    {
        return -ENOMEM;
    }

    for (uint16_t i = 0; i < count; i++)
    {
        struct zcbor_string name_str = {
            .value = values[i].name,
            .len = strlen(values[i].name),
        };

        if (!zcbor_map_start_encode(encoder, 2) || !zcbor_tstr_put_lit(encoder, "value") ||
            !zcbor_int32_put(encoder, values[i].value) || !zcbor_tstr_put_lit(encoder, "name") ||
            !zcbor_tstr_encode(encoder, &name_str) || !zcbor_map_end_encode(encoder, 2))
        {
            return -ENOMEM;
        }
    }

    if (!zcbor_list_end_encode(encoder, count))
    {
        return -ENOMEM;
    }

    return SUCCESS;
}

//**********************************************************
//* Public Function Definitions
//**********************************************************

const struct datastore_item_interface datastore_enum_interface = {
    .validate = validate,
    .is_equal = is_equal,
    .set = set,
    .get = get,
    .release = release,
    .decode = decode,
    .encode = encode,
    .encode_constraints = encode_constraints,
};

int enum_get_name_from_value(const union datastore_constraints* constraints, int value, char** out_name)
{
    int ret = -EINVAL;

    for (int i = 0; i < constraints->enum_constraints.value_count; i++)
    {
        struct data_enum_value enum_value = constraints->enum_constraints.values[i];
        if (enum_value.value == value)
        {
            *out_name = enum_value.name;
            ret = SUCCESS;
            break;
        }
    }

    return ret;
}

int enum_get_value_from_name(const union datastore_constraints* constraints, char* name, int* out_value)
{
    int ret = -EINVAL;

    for (int i = 0; i < constraints->enum_constraints.value_count; i++)
    {
        struct data_enum_value enum_value = constraints->enum_constraints.values[i];
        // SAFETY: The enum value names are constant always terminated, strcmp will end.
        if (strcmp(enum_value.name, name) == 0)
        {
            *out_value = enum_value.value;
            ret = SUCCESS;
            break;
        }
    }

    return ret;
}