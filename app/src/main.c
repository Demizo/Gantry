#include <autoconf.h>
#include <zephyr/app_version.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "buffer.h"
#include "datastore.h"
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
    int version_code = 0;
    datastore_get(DATASTORE_ID_VERSION_CODE, (void**)&version_code);
    LOG_INF("Version code %d", version_code);
    datastore_release(DATASTORE_ID_VERSION_CODE, (void**)&version_code);

    char* device_name = NULL;
    datastore_get(DATASTORE_ID_DEVICE_NAME, (void**)&device_name);
    LOG_INF("Device name %s", device_name);
    datastore_release(DATASTORE_ID_DEVICE_NAME, (void**)&device_name);

    float test_float = 0;
    datastore_get(DATASTORE_ID_TEST_FLOAT, (void**)&test_float);
    LOG_INF("Test float %f", (double)test_float);
    datastore_release(DATASTORE_ID_TEST_FLOAT, (void**)&test_float);

    buffer_t* test_bytes = NULL;
    datastore_get(DATASTORE_ID_TEST_BYTES, (void**)&test_bytes);
    LOG_HEXDUMP_INF(test_bytes->buf, test_bytes->len, "Test bytes");
    datastore_release(DATASTORE_ID_TEST_BYTES, (void**)&test_bytes);

    // Test setting datastore items
    int new_version_code = 2;
    (void)datastore_set(DATASTORE_ID_VERSION_CODE, (void*)&new_version_code);
    datastore_get(DATASTORE_ID_VERSION_CODE, (void**)&version_code);
    LOG_INF("Version code %d", version_code);
    datastore_release(DATASTORE_ID_VERSION_CODE, (void**)&version_code);

    (void)datastore_set(DATASTORE_ID_DEVICE_NAME, (void*)"New ZDS awesome name!");
    datastore_get(DATASTORE_ID_DEVICE_NAME, (void**)&device_name);
    LOG_INF("Device name %s", device_name);
    datastore_release(DATASTORE_ID_DEVICE_NAME, (void**)&device_name);

    float new_test_float = 24.5f;
    (void)datastore_set(DATASTORE_ID_TEST_FLOAT, (void*)&new_test_float);
    datastore_get(DATASTORE_ID_TEST_FLOAT, (void**)&test_float);
    LOG_INF("Test float %f", (double)test_float);
    datastore_release(DATASTORE_ID_TEST_FLOAT, (void**)&test_float);

    STACK_BUFFER(new_test_bytes, 6);
    datastore_set(DATASTORE_ID_TEST_BYTES, (void*)&new_test_bytes);
    (void)datastore_get(DATASTORE_ID_TEST_BYTES, (void**)&test_bytes);
    LOG_HEXDUMP_INF(test_bytes->buf, test_bytes->len, "Test bytes");
    datastore_release(DATASTORE_ID_TEST_BYTES, (void**)&test_bytes);

    while (1)
    {
        k_sleep(K_MSEC(100));
    }

    return 0;
}
