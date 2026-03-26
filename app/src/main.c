#include <autoconf.h>
#include <zephyr/app_version.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "buffer.h"
#include "datastore.h"
#include "datastore_types.h"
#include "generated_datastore_items.h"
#include "memory.h"

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

//**********************************************************
//* Static Variable Definitions
//**********************************************************

//**********************************************************
//* Static Function Definitions
//**********************************************************

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
    data_value_t version_code = { 0 };
    datastore_get(DATASTORE_ID_VERSION_CODE, &version_code);
    LOG_INF("Version code %d", version_code.data.int_value);
    datastore_release(DATASTORE_ID_VERSION_CODE, &version_code);

    data_value_t device_name = { 0 };
    datastore_get(DATASTORE_ID_DEVICE_NAME, &device_name);
    LOG_INF("Device name %s", device_name.data.string_value);
    datastore_release(DATASTORE_ID_DEVICE_NAME, &device_name);

    data_value_t test_float = { 0 };
    datastore_get(DATASTORE_ID_TEST_FLOAT, &test_float);
    LOG_INF("Test float %f", (double)test_float.data.float_value);
    datastore_release(DATASTORE_ID_TEST_FLOAT, &test_float);

    data_value_t test_bytes = { 0 };
    datastore_get(DATASTORE_ID_TEST_BYTES, &test_bytes);
    LOG_HEXDUMP_INF(test_bytes.data.buffer_value->buf, test_bytes.data.buffer_value->len, "Test bytes");
    datastore_release(DATASTORE_ID_TEST_BYTES, &test_bytes);

    data_value_t test_buffer = { 0 };
    datastore_get(DATASTORE_ID_TEST_BUFFER, &test_buffer);
    LOG_HEXDUMP_INF(test_buffer.data.buffer_value->buf, test_buffer.data.buffer_value->len, "Test buffer");
    datastore_release(DATASTORE_ID_TEST_BUFFER, &test_buffer);

    // Test setting datastore items
    data_value_t new_version_code = {
        .type = DATASTORE_ITEM_TYPE_INT,
        .data.int_value = 2,
    };
    (void)datastore_set(DATASTORE_ID_VERSION_CODE, new_version_code);
    datastore_get(DATASTORE_ID_VERSION_CODE, &version_code);
    LOG_INF("Version code %d", version_code.data.int_value);
    datastore_release(DATASTORE_ID_VERSION_CODE, &version_code);

    data_value_t new_device_name = {
        .type = DATASTORE_ITEM_TYPE_STRING,
        .data.string_value = "New ZDS awesome name!!!!!",
    };
    (void)datastore_set(DATASTORE_ID_DEVICE_NAME, new_device_name);
    datastore_get(DATASTORE_ID_DEVICE_NAME, &device_name);
    LOG_INF("Device name %s", device_name.data.string_value);
    datastore_release(DATASTORE_ID_DEVICE_NAME, &device_name);

    data_value_t new_test_float = {
        .type = DATASTORE_ITEM_TYPE_FLOAT,
        .data.float_value = 24.5f,
    };
    (void)datastore_set(DATASTORE_ID_TEST_FLOAT, new_test_float);
    datastore_get(DATASTORE_ID_TEST_FLOAT, &test_float);
    LOG_INF("Test float %f", (double)test_float.data.float_value);
    datastore_release(DATASTORE_ID_TEST_FLOAT, &test_float);

    STACK_BUFFER(new_test_bytes_data, 6);
    data_value_t new_test_bytes = {
        .type = DATASTORE_ITEM_TYPE_BYTE_ARRAY,
        .data.buffer_value = new_test_bytes_data,
    };
    datastore_set(DATASTORE_ID_TEST_BYTES, new_test_bytes);
    (void)datastore_get(DATASTORE_ID_TEST_BYTES, &test_bytes);
    LOG_HEXDUMP_INF(test_bytes.data.buffer_value->buf, test_bytes.data.buffer_value->len, "Test bytes");
    datastore_release(DATASTORE_ID_TEST_BYTES, &test_bytes);

    STACK_BUFFER(new_test_buffer_data, 6);
    new_test_buffer_data->buf[0] = 0xB5;
    data_value_t new_test_buffer = {
        .type = DATASTORE_ITEM_TYPE_BYTE_BUFFER,
        .data.buffer_value = new_test_buffer_data,
    };
    datastore_set(DATASTORE_ID_TEST_BUFFER, new_test_buffer);
    (void)datastore_get(DATASTORE_ID_TEST_BUFFER, &test_buffer);
    LOG_HEXDUMP_INF(test_buffer.data.buffer_value->buf, test_buffer.data.buffer_value->len, "Test buffer");
    datastore_release(DATASTORE_ID_TEST_BUFFER, &test_buffer);

    while (1)
    {
        k_sleep(K_MSEC(100));
    }

    return 0;
}
