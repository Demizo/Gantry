/**
 * @file datastore_type_byte_array.c
 * @author Demizo (demizodemazo@gmail.com)
 * @brief Byte array type for datastore items
 * @version 0.1
 * @date 2026-03-01
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "datastore_type_byte_array.h"

#include <stdint.h>
#include <sys/errno.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "buffer.h"
#include "datastore_types.h"
#include "error.h"
#include "memory.h"

/**
 * @brief Logger for module
 */
LOG_MODULE_REGISTER(datastore_type_byte_array, CONFIG_DATASTORE_TYPES_LOG_LEVEL);

//**********************************************************
//* Local Definitions
//**********************************************************

//**********************************************************
//* Static Function Declarations
//**********************************************************

static bool validate(const union datastore_constraints* constraints, data_value_t value);
static bool is_default(const struct datastore_item_const_metadata* item);
static void set(const struct datastore_item_const_metadata* item, data_value_t value);
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
    ASSERT(value.type == DATASTORE_ITEM_TYPE_BYTE_ARRAY, "Unexpected value type");
    const buffer_t* new_value = value.data.buffer_value;
    return (
        (new_value->len >= constraints->buffer_constraints.min_len) &&
        (new_value->len <= constraints->buffer_constraints.max_len));
}

static bool is_default(const struct datastore_item_const_metadata* item)
{
    buffer_t* current_value = (buffer_t*)item->value_ptr;
    buffer_t* default_value = item->default_value.buffer_value;

    if (current_value->len != default_value->len) return false;
    return (memcmp(current_value->buf, default_value->buf, default_value->len) == 0);
}

static void set(const struct datastore_item_const_metadata* item, data_value_t value)
{
    ASSERT(value.type == DATASTORE_ITEM_TYPE_BYTE_ARRAY, "Unexpected value type");
    buffer_t* current_value = (buffer_t*)item->value_ptr;
    buffer_t* new_value = value.data.buffer_value;

    current_value->len = new_value->len;
    memcpy(current_value->buf, new_value->buf, new_value->len);
}

static int get(const struct datastore_item_const_metadata* item, data_value_t* out_value)
{
    int ret = SUCCESS;
    buffer_t* current_value = (buffer_t*)item->value_ptr;
    uint16_t len = current_value->len;

    void* buffer_block = NULL;
    ret = mem_alloc(sizeof(buffer_t) + len, &buffer_block);
    if (ret == SUCCESS)
    {
        buffer_t* buffer = (buffer_t*)buffer_block;
        buffer->len = len;
        memcpy(buffer->buf, current_value->buf, len);

        out_value->type = DATASTORE_ITEM_TYPE_BYTE_ARRAY;
        out_value->data.buffer_value = buffer_block;
    }

    PASS_OWNERSHIP(buffer_block);
    return ret;
}

static void release(data_value_t* value)
{
    ASSERT(value->type == DATASTORE_ITEM_TYPE_BYTE_ARRAY, "Unexpected value type");

    void* buffer_block = value->data.buffer_value;
    mem_unref(&buffer_block);
}

static int encode(zcbor_state_t* encoder, data_value_t value)
{
    ASSERT(value.type == DATASTORE_ITEM_TYPE_BYTE_ARRAY, "Unexpected value type");

    const buffer_t* buffer = value.data.buffer_value;
    struct zcbor_string str = { .value = buffer->buf, .len = buffer->len };

    if (!zcbor_bstr_encode(encoder, &str))
    {
        return -ENOMEM;
    }

    return SUCCESS;
}

static int decode(zcbor_state_t* decoder, data_value_t* out_value)
{
    struct zcbor_string str;

    if (!zcbor_bstr_decode(decoder, &str))
    {
        return -EBADMSG;
    }

    void* new_buffer_block = NULL;
    int ret = mem_alloc(sizeof(buffer_t) + str.len, &new_buffer_block);
    if (ret != SUCCESS)
    {
        NOT_REFERENCED(new_buffer_block);
        return ret;
    }

    buffer_t* new_buffer = (buffer_t*)new_buffer_block;
    new_buffer->len = str.len;
    memcpy(new_buffer->buf, str.value, str.len);

    out_value->type = DATASTORE_ITEM_TYPE_BYTE_ARRAY;
    out_value->data.buffer_value = new_buffer;

    PASS_OWNERSHIP(new_buffer_block);
    return SUCCESS;
}

static int encode_constraints(zcbor_state_t* encoder, const union datastore_constraints* constraints)
{
    if (!zcbor_map_start_encode(encoder, 2) || !zcbor_tstr_put_lit(encoder, "min_len") ||
        !zcbor_uint32_put(encoder, constraints->buffer_constraints.min_len) ||
        !zcbor_tstr_put_lit(encoder, "max_len") ||
        !zcbor_uint32_put(encoder, constraints->buffer_constraints.max_len) || !zcbor_map_end_encode(encoder, 2))
    {
        return -ENOMEM;
    }

    return SUCCESS;
}

//**********************************************************
//* Public Function Definitions
//**********************************************************

const struct datastore_item_interface datastore_byte_array_interface = {
    .validate = validate,
    .is_default = is_default,
    .set = set,
    .get = get,
    .release = release,
    .decode = decode,
    .encode = encode,
    .encode_constraints = encode_constraints,
};
