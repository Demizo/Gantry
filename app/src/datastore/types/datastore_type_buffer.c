/**
 * @file datastore_type_buffer.c
 * @author Demizo (demizodemazo@gmail.com)
 * @brief Buffer type for datastore items
 * @version 0.1
 * @date 2026-03-01
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "datastore_type_buffer.h"

#include <sys/errno.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "buffer.h"
#include "datastore_types.h"
#include "error.h"
#include "memory.h"
#include "zephyr/toolchain.h"

LOG_MODULE_REGISTER(datastore_type_buffer, CONFIG_DATASTORE_TYPES_LOG_LEVEL);

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
    ASSERT(value.type == DATASTORE_ITEM_TYPE_BUFFER, "Unexpected value type");
    const buffer_t* new_value = value.data.buffer_value;
    return (
        (new_value->len >= item->type_info.buffer_info.min_len) &&
        (new_value->len <= item->type_info.buffer_info.max_len));
}

static void set(const struct datastore_item_const_metadata* item, data_value_t value)
{
    ASSERT(value.type == DATASTORE_ITEM_TYPE_BUFFER, "Unexpected value type");
    int ret = SUCCESS;
    buffer_t* buffer = value.data.buffer_value;

    // Create new buffer block
    void* new_buffer_block = NULL;
    ret = MEM_ALLOC(sizeof(buffer_t) + buffer->len, &new_buffer_block);
    if (ret != SUCCESS)
    {
        // Block was not allocated
        PASS_OWNERSHIP(new_buffer_block);
        return;
    }

    // Copy over buffer
    buffer_t* new_buffer = (buffer_t*)new_buffer_block;
    new_buffer->len = buffer->len;
    memcpy(new_buffer->buf, buffer->buf, buffer->len);

    // Unreference old buffer block
    void* old_buffer_block = *(void**)item->value_ptr;
    (void)MEM_UNREF(&old_buffer_block);

    // Set item to new buffer
    *(buffer_t**)(item->value_ptr) = (buffer_t*)new_buffer_block;
    PASS_OWNERSHIP(new_buffer_block);
}

static int get(const struct datastore_item_const_metadata* item, data_value_t* out_value)
{
    int ret = SUCCESS;
    void* current_buffer_block = *(void**)item->value_ptr;

    ret = MEM_REF(current_buffer_block);
    if (ret == SUCCESS)
    {
        out_value->type = DATASTORE_ITEM_TYPE_BUFFER;
        out_value->data.buffer_value = current_buffer_block;
    }

    PASS_OWNERSHIP(current_buffer_block);
    return ret;
}

static int release(const struct datastore_item_const_metadata* item, data_value_t* value)
{
    ARG_UNUSED(item);
    ASSERT(value->type == DATASTORE_ITEM_TYPE_BUFFER, "Unexpected value type");

    void* buffer_block = value->data.buffer_value;
    return MEM_UNREF(&buffer_block);
}

//**********************************************************
//* Public Function Definitions
//**********************************************************

const struct datastore_item_interface datastore_buffer_interface = {
    .validate = validate,
    .set = set,
    .get = get,
    .release = release,
};
