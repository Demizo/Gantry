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

LOG_MODULE_REGISTER(datastore_type_string, CONFIG_DATASTORE_TYPES_LOG_LEVEL);

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
    ASSERT(value.type == DATASTORE_ITEM_TYPE_STRING, "Unexpected value type");
    uint16_t len = strnlen(value.data.string_value, item->constraints.buffer_constraints.max_len + 1);
    // The string buffer is one byte larger than max length so that there is room for the null terminator
    return (
        (len >= item->constraints.buffer_constraints.min_len) && (len <= item->constraints.buffer_constraints.max_len));
}

static bool is_default(const struct datastore_item_const_metadata* item)
{
    char* current_value = (char*)item->value_ptr;
    char* default_value = item->default_value.string_value;

    return (strncmp(current_value, default_value, item->constraints.buffer_constraints.max_len) == 0);
}

static void set(const struct datastore_item_const_metadata* item, data_value_t value)
{
    ASSERT(value.type == DATASTORE_ITEM_TYPE_STRING, "Unexpected value type");
    strncpy((char*)item->value_ptr, value.data.string_value, item->constraints.buffer_constraints.max_len);
    ((char*)(item->value_ptr))[item->constraints.buffer_constraints.max_len] = '\0';
}

static int get(const struct datastore_item_const_metadata* item, data_value_t* out_value)
{
    int ret = SUCCESS;
    uint16_t len = strnlen((const char*)item->value_ptr, item->constraints.buffer_constraints.max_len);

    void* string_block = NULL;
    ret = mem_alloc(len, &string_block);
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

static int release(data_value_t* value)
{
    ASSERT(value->type == DATASTORE_ITEM_TYPE_STRING, "Unexpected value type");

    void* string_block = value->data.string_value;
    return mem_unref(&string_block);
}

static int encode(zcbor_state_t* encoder, data_value_t value)
{
    ASSERT(value.type == DATASTORE_ITEM_TYPE_STRING, "Unexpected value type");

    const char* string_value = value.data.string_value;
    uint16_t len = strnlen(string_value, UINT16_MAX);
    struct zcbor_string str = { .value = string_value, .len = len };

    if (!zcbor_tstr_encode(encoder, &str))
    {
        return -ENOMEM;
    }

    return SUCCESS;
}

static int decode(zcbor_state_t* decoder, data_value_t* out_value)
{
    struct zcbor_string str;

    if (!zcbor_tstr_decode(decoder, &str))
    {
        return -EBADMSG;
    }

    void* string_block = NULL;
    int ret = mem_alloc(str.len, &string_block);
    if (ret != SUCCESS)
    {
        NOT_REFERENCED(string_block);
        return ret;
    }

    memcpy((char*)string_block, str.value, str.len);

    out_value->type = DATASTORE_ITEM_TYPE_STRING;
    out_value->data.string_value = string_block;

    PASS_OWNERSHIP(string_block);
    return SUCCESS;
}

//**********************************************************
//* Public Function Definitions
//**********************************************************

const struct datastore_item_interface datastore_string_interface = {
    .validate = validate,
    .is_default = is_default,
    .set = set,
    .get = get,
    .release = release,
    .decode = decode,
    .encode = encode,
};
