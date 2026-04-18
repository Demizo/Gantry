#include <generated_datastore_items.h>
#include <stdint.h>
#include <string.h>
#include <zcbor_decode.h>
#include <zcbor_encode.h>
#include <zephyr/ztest.h>

#include "datastore.h"
#include "datastore_describe.h"
#include "datastore_type_enum.h"
#include "zephyr/ztest_assert.h"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static void reset_datastore(void* fixture)
{
    (void)fixture;
    datastore_init();
}

// ---------------------------------------------------------------------------
// Suite: datastore_int
// ---------------------------------------------------------------------------

ZTEST_SUITE(datastore_int, NULL, NULL, reset_datastore, NULL, NULL);

ZTEST(datastore_int, test_get_default)
{
    data_value_t value;
    int ret = datastore_get(AUTH_ANY, DATASTORE_ID_TEST_INT, &value);
    zassert_equal(ret, 0);
    zassert_equal(value.type, DATASTORE_ITEM_TYPE_INT);
    zassert_equal(value.data.int_value, 0);
    datastore_release(DATASTORE_ID_TEST_INT, &value);
}

ZTEST(datastore_int, test_set_and_get)
{
    data_value_t set_value = { .type = DATASTORE_ITEM_TYPE_INT, .data.int_value = 25 };
    int ret = datastore_set(AUTH_ANY, DATASTORE_ID_TEST_INT, set_value);
    zassert_equal(ret, 0);

    data_value_t got;
    ret = datastore_get(AUTH_ANY, DATASTORE_ID_TEST_INT, &got);
    zassert_equal(ret, 0);
    zassert_equal(got.data.int_value, 25);
    datastore_release(DATASTORE_ID_TEST_INT, &got);
}

ZTEST(datastore_int, test_set_out_of_range_max)
{
    data_value_t value = { .type = DATASTORE_ITEM_TYPE_INT, .data.int_value = 51 };
    int ret = datastore_set(AUTH_ANY, DATASTORE_ID_TEST_INT, value);
    zassert_equal(ret, -EINVAL);
}

ZTEST(datastore_int, test_set_out_of_range_min)
{
    data_value_t value = { .type = DATASTORE_ITEM_TYPE_INT, .data.int_value = -51 };
    int ret = datastore_set(AUTH_ANY, DATASTORE_ID_TEST_INT, value);
    zassert_equal(ret, -EINVAL);
}

ZTEST(datastore_int, test_encode_decode)
{
    uint8_t buf[32];
    zcbor_state_t enc[2];
    zcbor_new_encode_state(enc, ARRAY_SIZE(enc), buf, sizeof(buf), 1);

    data_value_t val = { .type = DATASTORE_ITEM_TYPE_INT, .data.int_value = -10 };
    int ret = datastore_encode(enc, DATASTORE_ID_TEST_INT, val);
    zassert_equal(ret, 0);

    size_t encoded_len = enc[0].payload - buf;
    zcbor_state_t dec[2];
    zcbor_new_decode_state(dec, ARRAY_SIZE(dec), buf, encoded_len, 1, NULL, 0);

    data_value_t decoded;
    ret = datastore_decode(dec, DATASTORE_ID_TEST_INT, &decoded);
    zassert_equal(ret, 0);
    zassert_equal(decoded.type, DATASTORE_ITEM_TYPE_INT);
    zassert_equal(decoded.data.int_value, -10);
    datastore_release(DATASTORE_ID_TEST_INT, &decoded);
}

// ---------------------------------------------------------------------------
// Suite: datastore_float
// ---------------------------------------------------------------------------

ZTEST_SUITE(datastore_float, NULL, NULL, reset_datastore, NULL, NULL);

ZTEST(datastore_float, test_get_default)
{
    data_value_t value;
    int ret = datastore_get(AUTH_ANY, DATASTORE_ID_TEST_FLOAT, &value);
    zassert_equal(ret, 0);
    zassert_equal(value.type, DATASTORE_ITEM_TYPE_FLOAT);
    zassert_within(value.data.float_value, 1.0f, 0.001f);
    datastore_release(DATASTORE_ID_TEST_FLOAT, &value);
}

ZTEST(datastore_float, test_set_and_get)
{
    data_value_t set_value = { .type = DATASTORE_ITEM_TYPE_FLOAT, .data.float_value = 5.5f };
    int ret = datastore_set(AUTH_ANY, DATASTORE_ID_TEST_FLOAT, set_value);
    zassert_equal(ret, 0);

    data_value_t got;
    ret = datastore_get(AUTH_ANY, DATASTORE_ID_TEST_FLOAT, &got);
    zassert_equal(ret, 0);
    zassert_within(got.data.float_value, 5.5f, 0.001f);
    datastore_release(DATASTORE_ID_TEST_FLOAT, &got);
}

ZTEST(datastore_float, test_set_out_of_range)
{
    data_value_t value = { .type = DATASTORE_ITEM_TYPE_FLOAT, .data.float_value = 11.0f };
    int ret = datastore_set(AUTH_ANY, DATASTORE_ID_TEST_FLOAT, value);
    zassert_equal(ret, -EINVAL);
}

ZTEST(datastore_float, test_encode_decode)
{
    uint8_t buf[32];
    zcbor_state_t enc[2];
    zcbor_new_encode_state(enc, ARRAY_SIZE(enc), buf, sizeof(buf), 1);

    data_value_t val = { .type = DATASTORE_ITEM_TYPE_FLOAT, .data.float_value = 3.14f };
    int ret = datastore_encode(enc, DATASTORE_ID_TEST_FLOAT, val);
    zassert_equal(ret, 0);

    size_t encoded_len = enc[0].payload - buf;
    zcbor_state_t dec[2];
    zcbor_new_decode_state(dec, ARRAY_SIZE(dec), buf, encoded_len, 1, NULL, 0);

    data_value_t decoded;
    ret = datastore_decode(dec, DATASTORE_ID_TEST_FLOAT, &decoded);
    zassert_equal(ret, 0);
    zassert_within(decoded.data.float_value, 3.14f, 0.001f);
    datastore_release(DATASTORE_ID_TEST_FLOAT, &decoded);
}

// ---------------------------------------------------------------------------
// Suite: datastore_enum
// ---------------------------------------------------------------------------

ZTEST_SUITE(datastore_enum, NULL, NULL, reset_datastore, NULL, NULL);

ZTEST(datastore_enum, test_get_default)
{
    data_value_t value;
    int ret = datastore_get(AUTH_ANY, DATASTORE_ID_TEST_ENUM, &value);
    zassert_equal(ret, 0);
    zassert_equal(value.type, DATASTORE_ITEM_TYPE_ENUM);
    zassert_equal(value.data.int_value, Color_RED);
    datastore_release(DATASTORE_ID_TEST_ENUM, &value);
}

ZTEST(datastore_enum, test_set_valid)
{
    data_value_t val = { .type = DATASTORE_ITEM_TYPE_ENUM, .data.int_value = Color_BLUE };
    int ret = datastore_set(AUTH_ANY, DATASTORE_ID_TEST_ENUM, val);
    zassert_equal(ret, 0);

    data_value_t got;
    ret = datastore_get(AUTH_ANY, DATASTORE_ID_TEST_ENUM, &got);
    zassert_equal(ret, 0);
    zassert_equal(got.data.int_value, Color_BLUE);
    datastore_release(DATASTORE_ID_TEST_ENUM, &got);
}

ZTEST(datastore_enum, test_set_invalid)
{
    data_value_t val = { .type = DATASTORE_ITEM_TYPE_ENUM, .data.int_value = 99 };
    int ret = datastore_set(AUTH_ANY, DATASTORE_ID_TEST_ENUM, val);
    zassert_equal(ret, -EINVAL);
}

ZTEST(datastore_enum, test_name_from_value)
{
    const struct datastore_item_const_metadata* item = &g_datastore_const_metadata[DATASTORE_ID_TEST_ENUM];
    char* name = NULL;
    int ret = enum_get_name_from_value(item, Color_GREEN, &name);
    zassert_equal(ret, 0);
    zassert_str_equal(name, "GREEN");
}

ZTEST(datastore_enum, test_value_from_name)
{
    const struct datastore_item_const_metadata* item = &g_datastore_const_metadata[DATASTORE_ID_TEST_ENUM];
    int value = -1;
    int ret = enum_get_value_from_name(item, "BLUE", &value);
    zassert_equal(ret, 0);
    zassert_equal(value, Color_BLUE);
}

ZTEST(datastore_enum, test_encode_decode)
{
    uint8_t buf[32];
    zcbor_state_t enc[2];
    zcbor_new_encode_state(enc, ARRAY_SIZE(enc), buf, sizeof(buf), 1);

    data_value_t val = { .type = DATASTORE_ITEM_TYPE_ENUM, .data.int_value = Color_GREEN };
    int ret = datastore_encode(enc, DATASTORE_ID_TEST_ENUM, val);
    zassert_equal(ret, 0);

    size_t encoded_len = enc[0].payload - buf;
    zcbor_state_t dec[2];
    zcbor_new_decode_state(dec, ARRAY_SIZE(dec), buf, encoded_len, 1, NULL, 0);

    data_value_t decoded;
    ret = datastore_decode(dec, DATASTORE_ID_TEST_ENUM, &decoded);
    zassert_equal(ret, 0);
    zassert_equal(decoded.data.int_value, Color_GREEN);
    datastore_release(DATASTORE_ID_TEST_ENUM, &decoded);
}

// ---------------------------------------------------------------------------
// Suite: datastore_string
// ---------------------------------------------------------------------------

ZTEST_SUITE(datastore_string, NULL, NULL, reset_datastore, NULL, NULL);

ZTEST(datastore_string, test_get_default)
{
    data_value_t value;
    int ret = datastore_get(AUTH_ANY, DATASTORE_ID_TEST_STRING, &value);
    zassert_equal(ret, 0);
    zassert_str_equal(value.data.string_value, "hello");
    datastore_release(DATASTORE_ID_TEST_STRING, &value);
}

ZTEST(datastore_string, test_set_and_get)
{
    data_value_t val = { .type = DATASTORE_ITEM_TYPE_STRING, .data.string_value = "world" };
    int ret = datastore_set(AUTH_SESSION, DATASTORE_ID_TEST_STRING, val);
    zassert_equal(ret, 0);

    data_value_t got;
    ret = datastore_get(AUTH_ANY, DATASTORE_ID_TEST_STRING, &got);
    zassert_equal(ret, 0);
    zassert_str_equal(got.data.string_value, "world");
    datastore_release(DATASTORE_ID_TEST_STRING, &got);
}

ZTEST(datastore_string, test_set_too_short)
{
    data_value_t val = { .type = DATASTORE_ITEM_TYPE_STRING, .data.string_value = "" };
    int ret = datastore_set(AUTH_SESSION, DATASTORE_ID_TEST_STRING, val);
    zassert_equal(ret, -EINVAL);
}

ZTEST(datastore_string, test_set_too_long)
{
    data_value_t val = {
        .type = DATASTORE_ITEM_TYPE_STRING,
        .data.string_value = "this_string_is_way_too_long_for_the_constraint_maximum",
    };
    int ret = datastore_set(AUTH_SESSION, DATASTORE_ID_TEST_STRING, val);
    zassert_equal(ret, -EINVAL);
}

ZTEST(datastore_string, test_encode_decode)
{
    uint8_t buf[64];
    zcbor_state_t enc[2];
    zcbor_new_encode_state(enc, ARRAY_SIZE(enc), buf, sizeof(buf), 1);

    data_value_t val = { .type = DATASTORE_ITEM_TYPE_STRING, .data.string_value = "test" };
    int ret = datastore_encode(enc, DATASTORE_ID_TEST_STRING, val);
    zassert_equal(ret, 0);

    size_t encoded_len = enc[0].payload - buf;
    zcbor_state_t dec[2];
    zcbor_new_decode_state(dec, ARRAY_SIZE(dec), buf, encoded_len, 1, NULL, 0);

    data_value_t decoded;
    ret = datastore_decode(dec, DATASTORE_ID_TEST_STRING, &decoded);
    zassert_equal(ret, 0);
    zassert_mem_equal(decoded.data.string_value, "test", 4);
    datastore_release(DATASTORE_ID_TEST_STRING, &decoded);
}

// ---------------------------------------------------------------------------
// Suite: datastore_byte_array
// ---------------------------------------------------------------------------

ZTEST_SUITE(datastore_byte_array, NULL, NULL, reset_datastore, NULL, NULL);

ZTEST(datastore_byte_array, test_get_default)
{
    data_value_t value;
    int ret = datastore_get(AUTH_ANY, DATASTORE_ID_TEST_BYTES, &value);
    zassert_equal(ret, 0);
    zassert_equal(value.type, DATASTORE_ITEM_TYPE_BYTE_ARRAY);
    zassert_equal(value.data.buffer_value->len, 4);
    zassert_equal(value.data.buffer_value->buf[0], 0x01);
    zassert_equal(value.data.buffer_value->buf[3], 0x04);
    datastore_release(DATASTORE_ID_TEST_BYTES, &value);
}

ZTEST(datastore_byte_array, test_set_and_get)
{
    uint8_t raw_buf[sizeof(buffer_t) + 4];
    buffer_t* buf = (buffer_t*)raw_buf;
    buf->len = 4;
    buf->buf[0] = 0xAA;
    buf->buf[1] = 0xBB;
    buf->buf[2] = 0xCC;
    buf->buf[3] = 0xDD;

    data_value_t val = { .type = DATASTORE_ITEM_TYPE_BYTE_ARRAY, .data.buffer_value = buf };
    int ret = datastore_set(AUTH_ANY, DATASTORE_ID_TEST_BYTES, val);
    zassert_equal(ret, 0);

    data_value_t got;
    ret = datastore_get(AUTH_ANY, DATASTORE_ID_TEST_BYTES, &got);
    zassert_equal(ret, 0);
    zassert_equal(got.data.buffer_value->len, 4);
    zassert_equal(got.data.buffer_value->buf[0], 0xAA);
    zassert_equal(got.data.buffer_value->buf[3], 0xDD);
    datastore_release(DATASTORE_ID_TEST_BYTES, &got);
}

ZTEST(datastore_byte_array, test_wrong_length)
{
    uint8_t raw_buf[sizeof(buffer_t) + 2];
    buffer_t* buf = (buffer_t*)raw_buf;
    buf->len = 2;

    data_value_t val = { .type = DATASTORE_ITEM_TYPE_BYTE_ARRAY, .data.buffer_value = buf };
    int ret = datastore_set(AUTH_ANY, DATASTORE_ID_TEST_BYTES, val);
    zassert_equal(ret, -EINVAL);
}

ZTEST(datastore_byte_array, test_encode_decode)
{
    uint8_t raw_buf[sizeof(buffer_t) + 4];
    buffer_t* buf = (buffer_t*)raw_buf;
    buf->len = 4;
    buf->buf[0] = 0x10;
    buf->buf[1] = 0x20;
    buf->buf[2] = 0x30;
    buf->buf[3] = 0x40;

    uint8_t cbor_buf[64];
    zcbor_state_t enc[2];
    zcbor_new_encode_state(enc, ARRAY_SIZE(enc), cbor_buf, sizeof(cbor_buf), 1);

    data_value_t val = { .type = DATASTORE_ITEM_TYPE_BYTE_ARRAY, .data.buffer_value = buf };
    int ret = datastore_encode(enc, DATASTORE_ID_TEST_BYTES, val);
    zassert_equal(ret, 0);

    size_t encoded_len = enc[0].payload - cbor_buf;
    zcbor_state_t dec[2];
    zcbor_new_decode_state(dec, ARRAY_SIZE(dec), cbor_buf, encoded_len, 1, NULL, 0);

    data_value_t decoded;
    ret = datastore_decode(dec, DATASTORE_ID_TEST_BYTES, &decoded);
    zassert_equal(ret, 0);
    zassert_equal(decoded.data.buffer_value->len, 4);
    zassert_equal(decoded.data.buffer_value->buf[0], 0x10);
    datastore_release(DATASTORE_ID_TEST_BYTES, &decoded);
}

// ---------------------------------------------------------------------------
// Suite: datastore_buffer
// ---------------------------------------------------------------------------

ZTEST_SUITE(datastore_buffer, NULL, NULL, reset_datastore, NULL, NULL);

ZTEST(datastore_buffer, test_get_default)
{
    data_value_t value;
    int ret = datastore_get(AUTH_ANY, DATASTORE_ID_TEST_BUFFER, &value);
    zassert_equal(ret, 0);
    zassert_equal(value.type, DATASTORE_ITEM_TYPE_BUFFER);
    zassert_equal(value.data.buffer_value->len, 0);
    datastore_release(DATASTORE_ID_TEST_BUFFER, &value);
}

ZTEST(datastore_buffer, test_set_and_get)
{
    uint8_t raw_buf[sizeof(buffer_t) + 8];
    buffer_t* buf = (buffer_t*)raw_buf;
    buf->len = 8;
    memset(buf->buf, 0x5A, 8);

    data_value_t val = { .type = DATASTORE_ITEM_TYPE_BUFFER, .data.buffer_value = buf };
    int ret = datastore_set(AUTH_ANY, DATASTORE_ID_TEST_BUFFER, val);
    zassert_equal(ret, 0);

    data_value_t got;
    ret = datastore_get(AUTH_ANY, DATASTORE_ID_TEST_BUFFER, &got);
    zassert_equal(ret, 0);
    zassert_equal(got.data.buffer_value->len, 8);
    zassert_equal(got.data.buffer_value->buf[0], 0x5A);
    datastore_release(DATASTORE_ID_TEST_BUFFER, &got);
}

ZTEST(datastore_buffer, test_set_exceeds_max)
{
    uint8_t raw_buf[sizeof(buffer_t) + 65];
    buffer_t* buf = (buffer_t*)raw_buf;
    buf->len = 65;

    data_value_t val = { .type = DATASTORE_ITEM_TYPE_BUFFER, .data.buffer_value = buf };
    int ret = datastore_set(AUTH_ANY, DATASTORE_ID_TEST_BUFFER, val);
    zassert_equal(ret, -EINVAL);
}

ZTEST(datastore_buffer, test_encode_decode)
{
    uint8_t raw_buf[sizeof(buffer_t) + 3];
    buffer_t* buf = (buffer_t*)raw_buf;
    buf->len = 3;
    buf->buf[0] = 0xCA;
    buf->buf[1] = 0xFE;
    buf->buf[2] = 0xBA;

    uint8_t cbor_buf[64];
    zcbor_state_t enc[2];
    zcbor_new_encode_state(enc, ARRAY_SIZE(enc), cbor_buf, sizeof(cbor_buf), 1);

    data_value_t val = { .type = DATASTORE_ITEM_TYPE_BUFFER, .data.buffer_value = buf };
    int ret = datastore_encode(enc, DATASTORE_ID_TEST_BUFFER, val);
    zassert_equal(ret, 0);

    size_t encoded_len = enc[0].payload - cbor_buf;
    zcbor_state_t dec[2];
    zcbor_new_decode_state(dec, ARRAY_SIZE(dec), cbor_buf, encoded_len, 1, NULL, 0);

    data_value_t decoded;
    ret = datastore_decode(dec, DATASTORE_ID_TEST_BUFFER, &decoded);
    zassert_equal(ret, 0);
    zassert_equal(decoded.data.buffer_value->len, 3);
    zassert_equal(decoded.data.buffer_value->buf[1], 0xFE);
    datastore_release(DATASTORE_ID_TEST_BUFFER, &decoded);
}

// ---------------------------------------------------------------------------
// Suite: datastore_access
// ---------------------------------------------------------------------------

ZTEST_SUITE(datastore_access, NULL, NULL, reset_datastore, NULL, NULL);

ZTEST(datastore_access, test_write_requires_session)
{
    data_value_t val = { .type = DATASTORE_ITEM_TYPE_STRING, .data.string_value = "newname" };
    int ret = datastore_set(AUTH_ANY, DATASTORE_ID_TEST_STRING, val);
    zassert_equal(ret, -EACCES);
}

ZTEST(datastore_access, test_write_with_session_succeeds)
{
    data_value_t val = { .type = DATASTORE_ITEM_TYPE_STRING, .data.string_value = "newname" };
    int ret = datastore_set(AUTH_SESSION, DATASTORE_ID_TEST_STRING, val);
    zassert_equal(ret, 0);
}

ZTEST(datastore_access, test_read_only_item_cannot_be_written_by_session)
{
    data_value_t val = { .type = DATASTORE_ITEM_TYPE_INT, .data.int_value = 10 };
    int ret = datastore_set(AUTH_SESSION, DATASTORE_ID_TEST_READ_ONLY, val);
    zassert_equal(ret, -EACCES);
}

ZTEST(datastore_access, test_internal_can_write_internal_item)
{
    data_value_t val = { .type = DATASTORE_ITEM_TYPE_INT, .data.int_value = 10 };
    int ret = datastore_set(AUTH_INTERNAL, DATASTORE_ID_TEST_READ_ONLY, val);
    zassert_equal(ret, 0);
}

ZTEST(datastore_access, test_tofu_can_be_written_from_default)
{
    data_value_t val = { .type = DATASTORE_ITEM_TYPE_STRING, .data.string_value = "myvalue" };
    int ret = datastore_set(AUTH_SESSION, DATASTORE_ID_TEST_TOFU, val);
    zassert_equal(ret, 0);
}

ZTEST(datastore_access, test_tofu_cannot_be_overwritten)
{
    data_value_t val1 = { .type = DATASTORE_ITEM_TYPE_STRING, .data.string_value = "first" };
    int ret = datastore_set(AUTH_SESSION, DATASTORE_ID_TEST_TOFU, val1);
    zassert_equal(ret, 0);

    data_value_t val2 = { .type = DATASTORE_ITEM_TYPE_STRING, .data.string_value = "second" };
    ret = datastore_set(AUTH_SESSION, DATASTORE_ID_TEST_TOFU, val2);
    zassert_equal(ret, -EACCES);
}

ZTEST(datastore_access, test_invalid_id)
{
    zassert_false(datastore_is_id_valid(DATASTORE_ID_COUNT));
    zassert_false(datastore_is_id_valid(0xFFFFFFFF));
}

ZTEST(datastore_access, test_valid_ids)
{
    for (uint32_t id = 0; id < DATASTORE_ID_COUNT; id++)
    {
        zassert_true(datastore_is_id_valid(id));
    }
}

// ---------------------------------------------------------------------------
// Suite: datastore_subscribe
// ---------------------------------------------------------------------------

static bool g_callback_called = false;
static int g_callback_handle_id = -1;
static int g_callback_copy_value = -1;

static void handle_callback(event_t* event)
{
    struct datastore_update_event_payload* payload = (struct datastore_update_event_payload*)event->data.buf;
    g_callback_called = true;
    g_callback_handle_id = (int)payload->metadata->id;
}

static void copy_callback(event_t* event)
{
    struct datastore_update_event_payload* payload = (struct datastore_update_event_payload*)event->data.buf;
    g_callback_called = true;
    g_callback_copy_value = payload->value_copy.data.int_value;
}

static void reset_datastore_and_flags(void* fixture)
{
    (void)fixture;
    datastore_init();
    g_callback_called = false;
    g_callback_handle_id = -1;
    g_callback_copy_value = -1;
}

ZTEST_SUITE(datastore_subscribe, NULL, NULL, reset_datastore_and_flags, NULL, NULL);

ZTEST(datastore_subscribe, test_handle_subscription)
{
    struct datastore_subscription sub = {
        .mode = DATASTORE_SUBSCRIPTION_HANDLE,
        .cb = handle_callback,
    };
    int ret = datastore_subscribe(AUTH_ANY, DATASTORE_ID_TEST_INT, &sub);
    zassert_equal(ret, 0);

    data_value_t val = { .type = DATASTORE_ITEM_TYPE_INT, .data.int_value = 7 };
    datastore_set(AUTH_ANY, DATASTORE_ID_TEST_INT, val);

    zassert_true(g_callback_called);
    zassert_equal(g_callback_handle_id, (int)DATASTORE_ID_TEST_INT);

    ret = datastore_unsubscribe(DATASTORE_ID_TEST_INT, &sub);
    zassert_equal(ret, 0);
}

ZTEST(datastore_subscribe, test_copy_subscription)
{
    struct datastore_subscription sub = {
        .mode = DATASTORE_SUBSCRIPTION_COPY,
        .cb = copy_callback,
    };
    int ret = datastore_subscribe(AUTH_ANY, DATASTORE_ID_TEST_INT, &sub);
    zassert_equal(ret, 0);

    data_value_t val = { .type = DATASTORE_ITEM_TYPE_INT, .data.int_value = 42 };
    datastore_set(AUTH_ANY, DATASTORE_ID_TEST_INT, val);

    zassert_true(g_callback_called);
    zassert_equal(g_callback_copy_value, 42);

    datastore_unsubscribe(DATASTORE_ID_TEST_INT, &sub);
}

ZTEST(datastore_subscribe, test_duplicate_subscription)
{
    struct datastore_subscription sub = {
        .mode = DATASTORE_SUBSCRIPTION_HANDLE,
        .cb = handle_callback,
    };
    int ret = datastore_subscribe(AUTH_ANY, DATASTORE_ID_TEST_INT, &sub);
    zassert_equal(ret, 0);

    ret = datastore_subscribe(AUTH_ANY, DATASTORE_ID_TEST_INT, &sub);
    zassert_equal(ret, -EALREADY);

    datastore_unsubscribe(DATASTORE_ID_TEST_INT, &sub);
}

ZTEST(datastore_subscribe, test_unsubscribe_not_subscribed)
{
    struct datastore_subscription sub = {
        .mode = DATASTORE_SUBSCRIPTION_HANDLE,
        .cb = handle_callback,
    };
    int ret = datastore_unsubscribe(DATASTORE_ID_TEST_INT, &sub);
    zassert_equal(ret, -ENOENT);
}

ZTEST(datastore_subscribe, test_no_callback_after_unsubscribe)
{
    struct datastore_subscription sub = {
        .mode = DATASTORE_SUBSCRIPTION_HANDLE,
        .cb = handle_callback,
    };
    datastore_subscribe(AUTH_ANY, DATASTORE_ID_TEST_INT, &sub);
    datastore_unsubscribe(DATASTORE_ID_TEST_INT, &sub);

    data_value_t val = { .type = DATASTORE_ITEM_TYPE_INT, .data.int_value = 5 };
    datastore_set(AUTH_ANY, DATASTORE_ID_TEST_INT, val);

    zassert_false(g_callback_called);
}

// ---------------------------------------------------------------------------
// Suite: datastore_describe
// ---------------------------------------------------------------------------

ZTEST_SUITE(datastore_describe, NULL, NULL, reset_datastore, NULL, NULL);

ZTEST(datastore_describe, test_full_describe_succeeds)
{
    uint8_t buf[4096];
    zcbor_state_t enc[4];
    zcbor_new_encode_state(enc, ARRAY_SIZE(enc), buf, sizeof(buf), DATASTORE_ID_COUNT);

    struct datastore_describe_state state;
    datastore_describe_start(&state);

    int ret = datastore_describe(&state, enc);
    zassert_equal(ret, 0);
    zassert_equal(state.current_id, DATASTORE_ID_COUNT);
}

ZTEST(datastore_describe, test_tiny_buffer_returns_enomem)
{
    uint8_t buf[1];
    zcbor_state_t enc[4];
    zcbor_new_encode_state(enc, ARRAY_SIZE(enc), buf, sizeof(buf), DATASTORE_ID_COUNT);

    struct datastore_describe_state state;
    datastore_describe_start(&state);

    int ret = datastore_describe(&state, enc);
    zassert_equal(ret, -ENOMEM);
    zassert_equal(state.current_id, 0);
}

ZTEST(datastore_describe, test_rollback_leaves_no_partial_data)
{
    uint8_t buf[8];
    zcbor_state_t enc[4];
    zcbor_new_encode_state(enc, ARRAY_SIZE(enc), buf, sizeof(buf), DATASTORE_ID_COUNT);

    const uint8_t* payload_before = enc[0].payload;

    struct datastore_describe_state state;
    datastore_describe_start(&state);
    datastore_describe(&state, enc);

    zassert_equal_ptr(enc[0].payload, payload_before);
}

ZTEST(datastore_describe, test_chunked_describe_covers_all_items)
{
    struct datastore_describe_state state;
    datastore_describe_start(&state);

    uint32_t items_encoded = 0;

    while (state.current_id < DATASTORE_ID_COUNT)
    {
        uint8_t buf[512];
        zcbor_state_t enc[4];
        zcbor_new_encode_state(enc, ARRAY_SIZE(enc), buf, sizeof(buf), DATASTORE_ID_COUNT);

        uint32_t id_before = state.current_id;
        int ret = datastore_describe(&state, enc);

        if (ret == 0)
        {
            items_encoded += DATASTORE_ID_COUNT - id_before;
            break;
        }
        else if (ret == -ENOMEM)
        {
            items_encoded += state.current_id - id_before;
        }
        else
        {
            zassert_unreachable("Unexpected return value from datastore_describe");
        }
    }

    zassert_equal(items_encoded, DATASTORE_ID_COUNT);
}

#define EXPECT_KEY(dec_state, expected_str)                                                                          \
    do                                                                                                               \
    {                                                                                                                \
        struct zcbor_string key;                                                                                     \
        zassert_true(zcbor_tstr_decode(dec_state, &key), "Failed to decode key");                                    \
        zassert_equal(key.len, strlen(expected_str), "Key length mismatch for %s", expected_str);                    \
        zassert_mem_equal(key.value, expected_str, strlen(expected_str), "Key mismatch, expected %s", expected_str); \
    } while (0)

ZTEST(datastore_describe, test_describe_encoding)
{
    uint8_t buf[256];
    zcbor_state_t enc[4];
    zcbor_new_encode_state(enc, ARRAY_SIZE(enc), buf, sizeof(buf), DATASTORE_ID_COUNT);

    struct datastore_describe_state state;
    datastore_describe_start(&state);

    int ret = datastore_describe(&state, enc);
    zassert_equal(ret, -ENOMEM);
    zassert_true(state.current_id > 0);

    ZCBOR_STATE_D(dec, 1, buf, enc->payload - buf, 1, 0);
    uint32_t val_u32;
    struct zcbor_string val_tstr;

    // Start map decode
    zassert_true(zcbor_map_start_decode(dec), "Failed to start map decode");

    // id
    EXPECT_KEY(dec, "id");
    zassert_true(zcbor_uint32_decode(dec, &val_u32), "Failed to decode id");

    // name
    EXPECT_KEY(dec, "name");
    zassert_true(zcbor_tstr_decode(dec, &val_tstr), "Failed to decode name");

    // storage
    EXPECT_KEY(dec, "storage");
    zassert_true(zcbor_uint32_decode(dec, &val_u32), "Failed to decode storage");

    // read_perm
    EXPECT_KEY(dec, "read_perm");
    zassert_true(zcbor_uint32_decode(dec, &val_u32), "Failed to decode read_perm");

    // write_perm
    EXPECT_KEY(dec, "write_perm");
    zassert_true(zcbor_uint32_decode(dec, &val_u32), "Failed to decode write_perm");

    // type
    EXPECT_KEY(dec, "type");
    zassert_true(zcbor_uint32_decode(dec, &val_u32), "Failed to decode type");

    // default (Skip the value)
    EXPECT_KEY(dec, "default");
    zassert_true(zcbor_any_skip(dec, NULL), "Failed to skip 'default' value");

    // constraints (Skip the value)
    EXPECT_KEY(dec, "constraints");
    zassert_true(zcbor_any_skip(dec, NULL), "Failed to skip 'constraints' value");

    // End map decode
    zassert_true(zcbor_map_end_decode(dec), "Failed to end map decode");
}
