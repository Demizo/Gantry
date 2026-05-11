/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <autoconf.h>
#include <zcbor_decode.h>
#include <gantry/buffer.h>
#include <gantry/error.h>
#include <gantry/event.h>
#include <gantry/memory.h>
#include <gantry/stow/stow.h>
#include <gantry/stow/stow_event.h>
#include <gantry/stow/types/stow_type_enum.h>
#include <gantry/stow/types/stow_types.h>
#include <zephyr/app_version.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "generated_stow_items.h"

LOG_MODULE_REGISTER(main, CONFIG_MAIN_LOG_LEVEL);

//**********************************************************
//* Local Definitions
//**********************************************************

//**********************************************************
//* Local typedefs
//**********************************************************

//**********************************************************
//* Static Function Declarations
//**********************************************************

void counter_cb(event_t* event);

//**********************************************************
//* Static Variable Definitions
//**********************************************************

static struct stow_subscription counter_handler = { .mode = STOW_SUBSCRIPTION_COPY, .cb = counter_cb };

//**********************************************************
//* Static Function Definitions
//**********************************************************

void counter_cb(event_t* event)
{
    ASSERT(event->type->id == EVENT_ID_STOW_UPDATE, "Unexpected event type");
    ASSERT(event->data.len == sizeof(struct stow_update_event_payload), "Unexpected payload size");
    struct stow_update_event_payload* payload = (struct stow_update_event_payload*)event->data.buf;
    ASSERT(payload->metadata->id == STOW_ID_TEST_INT, "Unexpected stow item");
    LOG_INF("Counter updated: %d", payload->value_copy.data.int_value);
}

//**********************************************************
//* Public Function Definitions
//**********************************************************

int main(void)
{
    LOG_INF("Starting Gantry example version %s", APP_VERSION_STRING);

    // Initialize modules
    stow_init();

    // Spawn threads
    void* mem_block = NULL;
    MEM_ALLOC(10, &mem_block);
    MEM_UNREF(&mem_block);

    // Test reading stow items
    data_value_t test_int = { 0 };
    STOW_GET(AUTH_INTERNAL, STOW_ID_TEST_INT, &test_int);
    LOG_INF("Test int %d", test_int.data.int_value);
    STOW_RELEASE(STOW_ID_TEST_INT, &test_int);

    data_value_t device_name = { 0 };
    STOW_GET(AUTH_INTERNAL, STOW_ID_DEVICE_NAME, &device_name);
    LOG_INF("Device name %s", device_name.data.string_value);
    STOW_RELEASE(STOW_ID_DEVICE_NAME, &device_name);

    data_value_t test_float = { 0 };
    STOW_GET(AUTH_INTERNAL, STOW_ID_TEST_FLOAT, &test_float);
    LOG_INF("Test float %f", (double)test_float.data.float_value);
    STOW_RELEASE(STOW_ID_TEST_FLOAT, &test_float);

    data_value_t test_bytes = { 0 };
    STOW_GET(AUTH_INTERNAL, STOW_ID_TEST_BYTES, &test_bytes);
    LOG_HEXDUMP_INF(test_bytes.data.buffer_value->buf, test_bytes.data.buffer_value->len, "Test bytes");
    STOW_RELEASE(STOW_ID_TEST_BYTES, &test_bytes);

    data_value_t test_buffer = { 0 };
    STOW_GET(AUTH_INTERNAL, STOW_ID_TEST_BUFFER, &test_buffer);
    LOG_HEXDUMP_INF(test_buffer.data.buffer_value->buf, test_buffer.data.buffer_value->len, "Test buffer");
    STOW_RELEASE(STOW_ID_TEST_BUFFER, &test_buffer);

    data_value_t test_enum = { 0 };
    STOW_GET(AUTH_INTERNAL, STOW_ID_TEST_ENUM, &test_enum);
    char* name = NULL;
    (void)enum_get_name_from_value(
        &g_stow_const_metadata[STOW_ID_TEST_ENUM].constraints, test_enum.data.int_value, &name);
    LOG_INF("Test enum %d (%s)", test_enum.data.int_value, name);
    STOW_RELEASE(STOW_ID_TEST_ENUM, &test_enum);

    // Test setting stow items
    data_value_t new_test_int = {
        .type = STOW_ITEM_TYPE_INT,
        .data.int_value = 2,
    };
    (void)STOW_SET(AUTH_INTERNAL, STOW_ID_TEST_INT, new_test_int);
    STOW_GET(AUTH_INTERNAL, STOW_ID_TEST_INT, &test_int);
    LOG_INF("Test int %d", test_int.data.int_value);
    STOW_RELEASE(STOW_ID_TEST_INT, &test_int);

    data_value_t new_device_name = {
        .type = STOW_ITEM_TYPE_STRING,
        .data.string_value = "New big awesome name!!!!!",
    };
    (void)STOW_SET(AUTH_INTERNAL, STOW_ID_DEVICE_NAME, new_device_name);
    STOW_GET(AUTH_INTERNAL, STOW_ID_DEVICE_NAME, &device_name);
    LOG_INF("Device name %s", device_name.data.string_value);
    STOW_RELEASE(STOW_ID_DEVICE_NAME, &device_name);

    data_value_t new_test_float = {
        .type = STOW_ITEM_TYPE_FLOAT,
        .data.float_value = 24.5f,
    };
    (void)STOW_SET(AUTH_INTERNAL, STOW_ID_TEST_FLOAT, new_test_float);
    STOW_GET(AUTH_INTERNAL, STOW_ID_TEST_FLOAT, &test_float);
    LOG_INF("Test float %f", (double)test_float.data.float_value);
    STOW_RELEASE(STOW_ID_TEST_FLOAT, &test_float);

    STACK_BUFFER(new_test_bytes_data, 12);
    data_value_t new_test_bytes = {
        .type = STOW_ITEM_TYPE_BYTE_ARRAY,
        .data.buffer_value = new_test_bytes_data,
    };
    STOW_SET(AUTH_INTERNAL, STOW_ID_TEST_BYTES, new_test_bytes);
    (void)STOW_GET(AUTH_INTERNAL, STOW_ID_TEST_BYTES, &test_bytes);
    LOG_HEXDUMP_INF(test_bytes.data.buffer_value->buf, test_bytes.data.buffer_value->len, "Test bytes");
    STOW_RELEASE(STOW_ID_TEST_BYTES, &test_bytes);

    STACK_BUFFER(new_test_buffer_data, 6);
    new_test_buffer_data->buf[0] = 0xB5;
    data_value_t new_test_buffer = {
        .type = STOW_ITEM_TYPE_BUFFER,
        .data.buffer_value = new_test_buffer_data,
    };
    STOW_SET(AUTH_INTERNAL, STOW_ID_TEST_BUFFER, new_test_buffer);
    (void)STOW_GET(AUTH_INTERNAL, STOW_ID_TEST_BUFFER, &test_buffer);
    LOG_HEXDUMP_INF(test_buffer.data.buffer_value->buf, test_buffer.data.buffer_value->len, "Test buffer");
    STOW_RELEASE(STOW_ID_TEST_BUFFER, &test_buffer);

    data_value_t new_test_enum = {
        .type = STOW_ITEM_TYPE_ENUM,
        .data.int_value = 1,
    };
    (void)STOW_SET(AUTH_INTERNAL, STOW_ID_TEST_ENUM, new_test_enum);
    STOW_GET(AUTH_INTERNAL, STOW_ID_TEST_ENUM, &test_enum);
    char* new_name = NULL;
    (void)enum_get_name_from_value(
        &g_stow_const_metadata[STOW_ID_TEST_ENUM].constraints, test_enum.data.int_value, &new_name);
    LOG_INF("Test enum %d (%s)", test_enum.data.int_value, new_name);
    STOW_RELEASE(STOW_ID_TEST_ENUM, &test_enum);

    // Test Encode/decode
    const uint16_t test_block_size = 1000;
    void* test_block = NULL;
    MEM_ALLOC(test_block_size, &test_block);

    ZCBOR_STATE_E(encoder, 1, test_block, test_block_size, 1);
    (void)STOW_GET(AUTH_INTERNAL, STOW_ID_TEST_BUFFER, &test_buffer);
    stow_encode(encoder, STOW_ID_TEST_BUFFER, test_buffer);
    LOG_HEXDUMP_INF(test_block, encoder->payload - (uint8_t*)test_block, "Encoded test buffer");
    STOW_RELEASE(STOW_ID_TEST_BUFFER, &test_buffer);

    ZCBOR_STATE_D(decoder, 1, test_block, test_block_size, 1, 0);
    data_value_t decoded_test_buffer;
    STOW_DECODE(decoder, STOW_ID_TEST_BUFFER, &decoded_test_buffer);
    LOG_HEXDUMP_INF(
        decoded_test_buffer.data.buffer_value->buf, decoded_test_buffer.data.buffer_value->len, "Decoded test buffer");
    STOW_RELEASE(STOW_ID_TEST_BUFFER, &decoded_test_buffer);

    MEM_UNREF(&test_block);

    int i = 0;
    (void)stow_subscribe(AUTH_INTERNAL, STOW_ID_TEST_INT, &counter_handler);
    while (1)
    {
        data_value_t counter = { .type = STOW_ITEM_TYPE_INT, .data.int_value = i };
        (void)STOW_SET(AUTH_INTERNAL, STOW_ID_TEST_INT, counter);
        LOG_INF("Counter set: %d", i);

        i++;
        if (i == 20)
        {
            (void)stow_unsubscribe(STOW_ID_TEST_INT, &counter_handler);
            LOG_INF("Unsubscribed");
        }

        k_sleep(K_MSEC(1000));
    }

    return 0;
}
