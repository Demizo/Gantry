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

#include <string.h>
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
LOG_MODULE_REGISTER(datastore_type_buffer, CONFIG_DATASTORE_TYPES_LOG_LEVEL);

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
        (new_value->len >= item->constraints.buffer_constraints.min_len) &&
        (new_value->len <= item->constraints.buffer_constraints.max_len));
}

static bool is_default(const struct datastore_item_const_metadata* item)
{
    buffer_t* current_value = *(buffer_t**)item->value_ptr;
    buffer_t* default_value = item->default_value.buffer_value;

    if (current_value->len != default_value->len) return false;
    return (memcmp(current_value->buf, default_value->buf, default_value->len) == 0);
}

static void set(const struct datastore_item_const_metadata* item, data_value_t value)
{
    ASSERT(value.type == DATASTORE_ITEM_TYPE_BUFFER, "Unexpected value type");
    int ret = SUCCESS;
    buffer_t* buffer = value.data.buffer_value;

    // Create new buffer block
    void* new_buffer_block = NULL;
    ret = mem_alloc(sizeof(buffer_t) + buffer->len, &new_buffer_block);
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
    mem_unref(&old_buffer_block);

    // Set item to new buffer
    *(buffer_t**)(item->value_ptr) = (buffer_t*)new_buffer_block;
    PASS_OWNERSHIP(new_buffer_block);
}

static int get(const struct datastore_item_const_metadata* item, data_value_t* out_value)
{
    void* current_buffer_block = *(void**)item->value_ptr;

    mem_ref(current_buffer_block);
    out_value->type = DATASTORE_ITEM_TYPE_BUFFER;
    out_value->data.buffer_value = current_buffer_block;

    PASS_OWNERSHIP(current_buffer_block);
    return SUCCESS;
}

static void release(data_value_t* value)
{
    ASSERT(value->type == DATASTORE_ITEM_TYPE_BUFFER, "Unexpected value type");

    void* buffer_block = value->data.buffer_value;
    mem_unref(&buffer_block);
}

static int encode(zcbor_state_t* encoder, data_value_t value)
{
    ASSERT(value.type == DATASTORE_ITEM_TYPE_BUFFER, "Unexpected value type");

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

    out_value->type = DATASTORE_ITEM_TYPE_BUFFER;
    out_value->data.buffer_value = new_buffer;

    PASS_OWNERSHIP(new_buffer_block);
    return SUCCESS;
}

//**********************************************************
//* Public Function Definitions
//**********************************************************

const struct datastore_item_interface datastore_buffer_interface = {
    .validate = validate,
    .is_default = is_default,
    .set = set,
    .get = get,
    .release = release,
    .decode = decode,
    .encode = encode,
};
