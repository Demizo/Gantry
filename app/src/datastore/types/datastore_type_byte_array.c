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
#include "zephyr/toolchain.h"

LOG_MODULE_REGISTER(datastore_type_byte_array, CONFIG_DATASTORE_TYPES_LOG_LEVEL);

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
    ASSERT(value.type == DATASTORE_ITEM_TYPE_BYTE_ARRAY, "Unexpected value type");
    const buffer_t* new_value = value.data.buffer_value;
    return (new_value->len == item->type_info.byte_array_info.size);
}

static void set(const struct datastore_item_const_metadata* item, data_value_t value)
{
    ASSERT(value.type == DATASTORE_ITEM_TYPE_BYTE_ARRAY, "Unexpected value type");
    buffer_t* buffer = value.data.buffer_value;
    memcpy(item->value_ptr, buffer->buf, buffer->len);
}

static int get(const struct datastore_item_const_metadata* item, data_value_t* out_value)
{
    int ret = SUCCESS;
    uint16_t len = item->type_info.byte_array_info.size;

    void* buffer_block = NULL;
    ret = MEM_ALLOC(sizeof(buffer_t) + len, &buffer_block);
    if (ret == SUCCESS)
    {
        buffer_t* buffer = (buffer_t*)buffer_block;
        buffer->len = len;
        memcpy(buffer->buf, item->value_ptr, len);

        out_value->type = DATASTORE_ITEM_TYPE_BYTE_ARRAY;
        out_value->data.buffer_value = buffer_block;
    }

    PASS_OWNERSHIP(buffer_block);
    return ret;
}

static int release(const struct datastore_item_const_metadata* item, data_value_t* value)
{
    ARG_UNUSED(item);
    ASSERT(value->type == DATASTORE_ITEM_TYPE_BYTE_ARRAY, "Unexpected value type");

    void* buffer_block = value->data.buffer_value;
    return MEM_UNREF(&buffer_block);
}

//**********************************************************
//* Public Function Definitions
//**********************************************************

const struct datastore_item_interface datastore_byte_array_interface = {
    .validate = validate,
    .set = set,
    .get = get,
    .release = release,
};
