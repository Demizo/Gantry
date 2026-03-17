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
    const buffer_t* new_value = (const buffer_t*)value;
    return (new_value->len == item->type_info.byte_array_info.size);
}

static void set(const struct datastore_item_const_metadata* item, const void* value)
{
    buffer_t* buffer = (buffer_t*)value;
    memcpy(item->value_ptr, buffer->buf, buffer->len);
}

static int get(const struct datastore_item_const_metadata* item, void** out_value)
{
    int ret = SUCCESS;
    uint16_t len = item->type_info.byte_array_info.size;

    ret = MEM_ALLOC(sizeof(buffer_t) + len, out_value);
    if (ret == SUCCESS)
    {
        buffer_t* buffer = (buffer_t*)*out_value;
        buffer->len = len;
        memcpy(buffer->buf, item->value_ptr, len);
    }

    PASS_OWNERSHIP(out_value);
    return ret;
}

static int release(const struct datastore_item_const_metadata* item, void** value)
{
    ARG_UNUSED(item);

    return MEM_UNREF(value);
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
