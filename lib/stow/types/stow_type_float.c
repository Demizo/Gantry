/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @author Demizo (demizodemazo@gmail.com)
 * @brief Float type for stow items
 *
 *
 */

#include <sys/errno.h>
#include <gantry/error.h>
#include <gantry/stow/types/stow_type_float.h>
#include <gantry/stow/types/stow_types.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

/**
 * @brief Logger for module
 */
LOG_MODULE_REGISTER(stow_type_float, CONFIG_STOW_TYPES_LOG_LEVEL);

//**********************************************************
//* Local Definitions
//**********************************************************

//**********************************************************
//* Static Function Declarations
//**********************************************************

static bool validate(const union stow_constraints* constraints, data_value_t value);
static bool is_equal(data_value_t a, data_value_t b);
static void set(void* dest, data_value_t value);
static int get(void* src, data_value_t* out_value);
static void release(data_value_t* value);
static int encode(zcbor_state_t* encoder, data_value_t value);
static int decode(zcbor_state_t* decoder, data_value_t* out_value);
static int encode_constraints(zcbor_state_t* encoder, const union stow_constraints* constraints);

//**********************************************************
//* Static Variable Definitions
//**********************************************************

//**********************************************************
//* Static Function Definitions
//**********************************************************

static bool validate(const union stow_constraints* constraints, data_value_t value)
{
    ASSERT(value.type == STOW_ITEM_TYPE_FLOAT, "Unexpected value type");
    const float new_value = value.data.float_value;
    return ((new_value >= constraints->float_constraints.min) && (new_value <= constraints->float_constraints.max));
}

static bool is_equal(data_value_t a, data_value_t b)
{
    ASSERT((a.type == STOW_ITEM_TYPE_FLOAT) && (b.type == STOW_ITEM_TYPE_FLOAT), "Unexpected value type");

    return a.data.float_value == b.data.float_value;
}

static void set(void* dest, data_value_t value)
{
    ASSERT(value.type == STOW_ITEM_TYPE_FLOAT, "Unexpected value type");
    *(float*)dest = value.data.float_value;
}

static int get(void* src, data_value_t* out_value)
{
    out_value->type = STOW_ITEM_TYPE_FLOAT;
    out_value->data.float_value = *(float*)src;
    return SUCCESS;
}

static void release(data_value_t* value)
{
    ASSERT(value->type == STOW_ITEM_TYPE_FLOAT, "Unexpected value type");
    // No action, nothing to free
}

static int encode(zcbor_state_t* encoder, data_value_t value)
{
    ASSERT(value.type == STOW_ITEM_TYPE_FLOAT, "Unexpected value type");

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

    out_value->type = STOW_ITEM_TYPE_FLOAT;
    out_value->data.float_value = decoded_value;

    return SUCCESS;
}

static int encode_constraints(zcbor_state_t* encoder, const union stow_constraints* constraints)
{
    if (!zcbor_map_start_encode(encoder, 2) || !zcbor_tstr_put_lit(encoder, "min") ||
        !zcbor_float32_put(encoder, constraints->float_constraints.min) || !zcbor_tstr_put_lit(encoder, "max") ||
        !zcbor_float32_put(encoder, constraints->float_constraints.max) || !zcbor_map_end_encode(encoder, 2))
    {
        return -ENOMEM;
    }

    return SUCCESS;
}

//**********************************************************
//* Public Function Definitions
//**********************************************************

const struct stow_item_interface stow_float_interface = {
    .validate = validate,
    .is_equal = is_equal,
    .set = set,
    .get = get,
    .release = release,
    .decode = decode,
    .encode = encode,
    .encode_constraints = encode_constraints,
};
