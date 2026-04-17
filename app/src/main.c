#include <autoconf.h>
#include <zephyr/app_version.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "buffer.h"
#include "datastore.h"
#include "datastore_event.h"
#include "datastore_type_enum.h"
#include "datastore_types.h"
#include "error.h"
#include "event.h"
#include "generated_datastore_items.h"
#include "memory.h"
#include "zcbor_decode.h"

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

static struct datastore_subscription counter_handler = { .mode = DATASTORE_SUBSCRIPTION_COPY, .cb = counter_cb };

//**********************************************************
//* Static Function Definitions
//**********************************************************

void counter_cb(event_t* event)
{
    ASSERT(event->type->id == EVENT_ID_DATASTORE_UPDATE, "Unexpected event type");
    ASSERT(event->data.len == sizeof(struct datastore_update_event_payload), "Unexpected payload size");
    struct datastore_update_event_payload* payload = (struct datastore_update_event_payload*)event->data.buf;
    ASSERT(payload->metadata->id == DATASTORE_ID_TEST_INT, "Unexpected datastore item");
    LOG_INF("Counter updated: %d", payload->value_copy.data.int_value);
}

//**********************************************************
//* Public Function Definitions
//**********************************************************

int main(void)
{
    LOG_INF("Starting ZDS version %s", APP_VERSION_STRING);

    // Initialize modules
    datastore_init();

    // Spawn threads
    void* mem_block = NULL;
    MEM_ALLOC(10, &mem_block);
    MEM_UNREF(&mem_block);

    // Test reading datastore items
    data_value_t test_int = { 0 };
    DATASTORE_GET(AUTH_INTERNAL, DATASTORE_ID_TEST_INT, &test_int);
    LOG_INF("Test int %d", test_int.data.int_value);
    DATASTORE_RELEASE(DATASTORE_ID_TEST_INT, &test_int);

    data_value_t device_name = { 0 };
    DATASTORE_GET(AUTH_INTERNAL, DATASTORE_ID_DEVICE_NAME, &device_name);
    LOG_INF("Device name %s", device_name.data.string_value);
    DATASTORE_RELEASE(DATASTORE_ID_DEVICE_NAME, &device_name);

    data_value_t test_float = { 0 };
    DATASTORE_GET(AUTH_INTERNAL, DATASTORE_ID_TEST_FLOAT, &test_float);
    LOG_INF("Test float %f", (double)test_float.data.float_value);
    DATASTORE_RELEASE(DATASTORE_ID_TEST_FLOAT, &test_float);

    data_value_t test_bytes = { 0 };
    DATASTORE_GET(AUTH_INTERNAL, DATASTORE_ID_TEST_BYTES, &test_bytes);
    LOG_HEXDUMP_INF(test_bytes.data.buffer_value->buf, test_bytes.data.buffer_value->len, "Test bytes");
    DATASTORE_RELEASE(DATASTORE_ID_TEST_BYTES, &test_bytes);

    data_value_t test_buffer = { 0 };
    DATASTORE_GET(AUTH_INTERNAL, DATASTORE_ID_TEST_BUFFER, &test_buffer);
    LOG_HEXDUMP_INF(test_buffer.data.buffer_value->buf, test_buffer.data.buffer_value->len, "Test buffer");
    DATASTORE_RELEASE(DATASTORE_ID_TEST_BUFFER, &test_buffer);

    data_value_t test_enum = { 0 };
    DATASTORE_GET(AUTH_INTERNAL, DATASTORE_ID_TEST_ENUM, &test_enum);
    char* name = NULL;
    (void)enum_get_name_from_value(
        &g_datastore_const_metadata[DATASTORE_ID_TEST_ENUM], test_enum.data.int_value, &name);
    LOG_INF("Test enum %d (%s)", test_enum.data.int_value, name);
    DATASTORE_RELEASE(DATASTORE_ID_TEST_ENUM, &test_enum);

    // Test setting datastore items
    data_value_t new_test_int = {
        .type = DATASTORE_ITEM_TYPE_INT,
        .data.int_value = 2,
    };
    (void)DATASTORE_SET(AUTH_INTERNAL, DATASTORE_ID_TEST_INT, new_test_int);
    DATASTORE_GET(AUTH_INTERNAL, DATASTORE_ID_TEST_INT, &test_int);
    LOG_INF("Test int %d", test_int.data.int_value);
    DATASTORE_RELEASE(DATASTORE_ID_TEST_INT, &test_int);

    data_value_t new_device_name = {
        .type = DATASTORE_ITEM_TYPE_STRING,
        .data.string_value = "New ZDS awesome name!!!!!",
    };
    (void)DATASTORE_SET(AUTH_INTERNAL, DATASTORE_ID_DEVICE_NAME, new_device_name);
    DATASTORE_GET(AUTH_INTERNAL, DATASTORE_ID_DEVICE_NAME, &device_name);
    LOG_INF("Device name %s", device_name.data.string_value);
    DATASTORE_RELEASE(DATASTORE_ID_DEVICE_NAME, &device_name);

    data_value_t new_test_float = {
        .type = DATASTORE_ITEM_TYPE_FLOAT,
        .data.float_value = 24.5f,
    };
    (void)DATASTORE_SET(AUTH_INTERNAL, DATASTORE_ID_TEST_FLOAT, new_test_float);
    DATASTORE_GET(AUTH_INTERNAL, DATASTORE_ID_TEST_FLOAT, &test_float);
    LOG_INF("Test float %f", (double)test_float.data.float_value);
    DATASTORE_RELEASE(DATASTORE_ID_TEST_FLOAT, &test_float);

    STACK_BUFFER(new_test_bytes_data, 12);
    data_value_t new_test_bytes = {
        .type = DATASTORE_ITEM_TYPE_BYTE_ARRAY,
        .data.buffer_value = new_test_bytes_data,
    };
    DATASTORE_SET(AUTH_INTERNAL, DATASTORE_ID_TEST_BYTES, new_test_bytes);
    (void)DATASTORE_GET(AUTH_INTERNAL, DATASTORE_ID_TEST_BYTES, &test_bytes);
    LOG_HEXDUMP_INF(test_bytes.data.buffer_value->buf, test_bytes.data.buffer_value->len, "Test bytes");
    DATASTORE_RELEASE(DATASTORE_ID_TEST_BYTES, &test_bytes);

    STACK_BUFFER(new_test_buffer_data, 6);
    new_test_buffer_data->buf[0] = 0xB5;
    data_value_t new_test_buffer = {
        .type = DATASTORE_ITEM_TYPE_BUFFER,
        .data.buffer_value = new_test_buffer_data,
    };
    DATASTORE_SET(AUTH_INTERNAL, DATASTORE_ID_TEST_BUFFER, new_test_buffer);
    (void)DATASTORE_GET(AUTH_INTERNAL, DATASTORE_ID_TEST_BUFFER, &test_buffer);
    LOG_HEXDUMP_INF(test_buffer.data.buffer_value->buf, test_buffer.data.buffer_value->len, "Test buffer");
    DATASTORE_RELEASE(DATASTORE_ID_TEST_BUFFER, &test_buffer);

    data_value_t new_test_enum = {
        .type = DATASTORE_ITEM_TYPE_ENUM,
        .data.int_value = 1,
    };
    (void)DATASTORE_SET(AUTH_INTERNAL, DATASTORE_ID_TEST_ENUM, new_test_enum);
    DATASTORE_GET(AUTH_INTERNAL, DATASTORE_ID_TEST_ENUM, &test_enum);
    char* new_name = NULL;
    (void)enum_get_name_from_value(
        &g_datastore_const_metadata[DATASTORE_ID_TEST_ENUM], test_enum.data.int_value, &new_name);
    LOG_INF("Test enum %d (%s)", test_enum.data.int_value, new_name);
    DATASTORE_RELEASE(DATASTORE_ID_TEST_ENUM, &test_enum);

    // Test Encode/decode
    const uint16_t test_block_size = 1000;
    void* test_block = NULL;
    MEM_ALLOC(test_block_size, &test_block);

    ZCBOR_STATE_E(encoder, 1, test_block, test_block_size, 1);
    (void)DATASTORE_GET(AUTH_INTERNAL, DATASTORE_ID_TEST_BUFFER, &test_buffer);
    datastore_encode(encoder, DATASTORE_ID_TEST_BUFFER, test_buffer);
    LOG_HEXDUMP_INF(test_block, encoder->payload - (uint8_t*)test_block, "Encoded test buffer");
    DATASTORE_RELEASE(DATASTORE_ID_TEST_BUFFER, &test_buffer);

    ZCBOR_STATE_D(decoder, 1, test_block, test_block_size, 1, 0);
    data_value_t decoded_test_buffer;
    DATASTORE_DECODE(decoder, DATASTORE_ID_TEST_BUFFER, &decoded_test_buffer);
    LOG_HEXDUMP_INF(
        decoded_test_buffer.data.buffer_value->buf, decoded_test_buffer.data.buffer_value->len, "Decoded test buffer");
    DATASTORE_RELEASE(DATASTORE_ID_TEST_BUFFER, &decoded_test_buffer);

    MEM_UNREF(&test_block);

    int i = 0;
    (void)datastore_subscribe(AUTH_INTERNAL, DATASTORE_ID_TEST_INT, &counter_handler);
    while (1)
    {
        data_value_t counter = { .type = DATASTORE_ITEM_TYPE_INT, .data.int_value = i };
        (void)DATASTORE_SET(AUTH_INTERNAL, DATASTORE_ID_TEST_INT, counter);
        LOG_INF("Counter set: %d", i);

        i++;
        if (i == 20)
        {
            (void)datastore_unsubscribe(DATASTORE_ID_TEST_INT, &counter_handler);
            LOG_INF("Unsubscribed");
        }

        k_sleep(K_MSEC(1000));
    }

    return 0;
}
