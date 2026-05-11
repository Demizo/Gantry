/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <generated_stow_enums.h>
#include <generated_stow_items.h>
#include <stdint.h>
#include <string.h>
#include <zcbor_decode.h>
#include <zcbor_encode.h>
#include <zds/memory.h>
#include <zds/stow/stow.h>
#include <zds/stow/stow_describe.h>
#include <zds/stow/types/stow_type_enum.h>
#include <zephyr/logging/log.h>
#include <zephyr/logging/log_core.h>
#include <zephyr/ztest.h>
#include <zephyr/ztest_assert.h>

#include "generated_struct_NestedStruct.h"
#include "generated_struct_TestStruct.h"

LOG_MODULE_REGISTER(test_stow, LOG_LEVEL_DBG);

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static void reset_stow(void* fixture)
{
    (void)fixture;
    stow_init();
}

// ---------------------------------------------------------------------------
// Suite: stow_int
// ---------------------------------------------------------------------------

ZTEST_SUITE(stow_int, NULL, NULL, reset_stow, NULL, NULL);

ZTEST(stow_int, test_get_default)
{
    data_value_t value;
    int ret = stow_get(AUTH_ANY, STOW_ID_TEST_INT, &value);
    zassert_equal(ret, 0);
    zassert_equal(value.type, STOW_ITEM_TYPE_INT);
    zassert_equal(value.data.int_value, 0);
    stow_release(STOW_ID_TEST_INT, &value);
}

ZTEST(stow_int, test_set_and_get)
{
    data_value_t set_value = { .type = STOW_ITEM_TYPE_INT, .data.int_value = 25 };
    int ret = stow_set(AUTH_ANY, STOW_ID_TEST_INT, set_value);
    zassert_equal(ret, 0);

    data_value_t got;
    ret = stow_get(AUTH_ANY, STOW_ID_TEST_INT, &got);
    zassert_equal(ret, 0);
    zassert_equal(got.data.int_value, 25);
    stow_release(STOW_ID_TEST_INT, &got);
}

ZTEST(stow_int, test_set_out_of_range_max)
{
    data_value_t value = { .type = STOW_ITEM_TYPE_INT, .data.int_value = 51 };
    int ret = stow_set(AUTH_ANY, STOW_ID_TEST_INT, value);
    zassert_equal(ret, -EINVAL);
}

ZTEST(stow_int, test_set_out_of_range_min)
{
    data_value_t value = { .type = STOW_ITEM_TYPE_INT, .data.int_value = -51 };
    int ret = stow_set(AUTH_ANY, STOW_ID_TEST_INT, value);
    zassert_equal(ret, -EINVAL);
}

ZTEST(stow_int, test_encode_decode)
{
    uint8_t buf[32];
    zcbor_state_t enc[2];
    zcbor_new_encode_state(enc, ARRAY_SIZE(enc), buf, sizeof(buf), 1);

    data_value_t val = { .type = STOW_ITEM_TYPE_INT, .data.int_value = -10 };
    int ret = stow_encode(enc, STOW_ID_TEST_INT, val);
    zassert_equal(ret, 0);

    size_t encoded_len = enc[0].payload - buf;
    zcbor_state_t dec[2];
    zcbor_new_decode_state(dec, ARRAY_SIZE(dec), buf, encoded_len, 1, NULL, 0);

    data_value_t decoded;
    ret = stow_decode(dec, STOW_ID_TEST_INT, &decoded);
    zassert_equal(ret, 0);
    zassert_equal(decoded.type, STOW_ITEM_TYPE_INT);
    zassert_equal(decoded.data.int_value, -10);
    stow_release(STOW_ID_TEST_INT, &decoded);
}

// ---------------------------------------------------------------------------
// Suite: stow_float
// ---------------------------------------------------------------------------

ZTEST_SUITE(stow_float, NULL, NULL, reset_stow, NULL, NULL);

ZTEST(stow_float, test_get_default)
{
    data_value_t value;
    int ret = stow_get(AUTH_ANY, STOW_ID_TEST_FLOAT, &value);
    zassert_equal(ret, 0);
    zassert_equal(value.type, STOW_ITEM_TYPE_FLOAT);
    zassert_within(value.data.float_value, 1.0f, 0.001f);
    stow_release(STOW_ID_TEST_FLOAT, &value);
}

ZTEST(stow_float, test_set_and_get)
{
    data_value_t set_value = { .type = STOW_ITEM_TYPE_FLOAT, .data.float_value = 5.5f };
    int ret = stow_set(AUTH_ANY, STOW_ID_TEST_FLOAT, set_value);
    zassert_equal(ret, 0);

    data_value_t got;
    ret = stow_get(AUTH_ANY, STOW_ID_TEST_FLOAT, &got);
    zassert_equal(ret, 0);
    zassert_within(got.data.float_value, 5.5f, 0.001f);
    stow_release(STOW_ID_TEST_FLOAT, &got);
}

ZTEST(stow_float, test_set_out_of_range)
{
    data_value_t value = { .type = STOW_ITEM_TYPE_FLOAT, .data.float_value = 11.0f };
    int ret = stow_set(AUTH_ANY, STOW_ID_TEST_FLOAT, value);
    zassert_equal(ret, -EINVAL);
}

ZTEST(stow_float, test_encode_decode)
{
    uint8_t buf[32];
    zcbor_state_t enc[2];
    zcbor_new_encode_state(enc, ARRAY_SIZE(enc), buf, sizeof(buf), 1);

    data_value_t val = { .type = STOW_ITEM_TYPE_FLOAT, .data.float_value = 3.14f };
    int ret = stow_encode(enc, STOW_ID_TEST_FLOAT, val);
    zassert_equal(ret, 0);

    size_t encoded_len = enc[0].payload - buf;
    zcbor_state_t dec[2];
    zcbor_new_decode_state(dec, ARRAY_SIZE(dec), buf, encoded_len, 1, NULL, 0);

    data_value_t decoded;
    ret = stow_decode(dec, STOW_ID_TEST_FLOAT, &decoded);
    zassert_equal(ret, 0);
    zassert_within(decoded.data.float_value, 3.14f, 0.001f);
    stow_release(STOW_ID_TEST_FLOAT, &decoded);
}

// ---------------------------------------------------------------------------
// Suite: stow_enum
// ---------------------------------------------------------------------------

ZTEST_SUITE(stow_enum, NULL, NULL, reset_stow, NULL, NULL);

ZTEST(stow_enum, test_get_default)
{
    data_value_t value;
    int ret = stow_get(AUTH_ANY, STOW_ID_TEST_ENUM, &value);
    zassert_equal(ret, 0);
    zassert_equal(value.type, STOW_ITEM_TYPE_ENUM);
    zassert_equal(value.data.int_value, Color_RED);
    stow_release(STOW_ID_TEST_ENUM, &value);
}

ZTEST(stow_enum, test_set_valid)
{
    data_value_t val = { .type = STOW_ITEM_TYPE_ENUM, .data.int_value = Color_BLUE };
    int ret = stow_set(AUTH_ANY, STOW_ID_TEST_ENUM, val);
    zassert_equal(ret, 0);

    data_value_t got;
    ret = stow_get(AUTH_ANY, STOW_ID_TEST_ENUM, &got);
    zassert_equal(ret, 0);
    zassert_equal(got.data.int_value, Color_BLUE);
    stow_release(STOW_ID_TEST_ENUM, &got);
}

ZTEST(stow_enum, test_set_invalid)
{
    data_value_t val = { .type = STOW_ITEM_TYPE_ENUM, .data.int_value = 99 };
    int ret = stow_set(AUTH_ANY, STOW_ID_TEST_ENUM, val);
    zassert_equal(ret, -EINVAL);
}

ZTEST(stow_enum, test_name_from_value)
{
    const struct stow_item_const_metadata* item = &g_stow_const_metadata[STOW_ID_TEST_ENUM];
    char* name = NULL;
    int ret = enum_get_name_from_value(&item->constraints, Color_GREEN, &name);
    zassert_equal(ret, 0);
    zassert_str_equal(name, "GREEN");
}

ZTEST(stow_enum, test_value_from_name)
{
    const struct stow_item_const_metadata* item = &g_stow_const_metadata[STOW_ID_TEST_ENUM];
    int value = -1;
    int ret = enum_get_value_from_name(&item->constraints, "BLUE", &value);
    zassert_equal(ret, 0);
    zassert_equal(value, Color_BLUE);
}

ZTEST(stow_enum, test_encode_decode)
{
    uint8_t buf[32];
    zcbor_state_t enc[2];
    zcbor_new_encode_state(enc, ARRAY_SIZE(enc), buf, sizeof(buf), 1);

    data_value_t val = { .type = STOW_ITEM_TYPE_ENUM, .data.int_value = Color_GREEN };
    int ret = stow_encode(enc, STOW_ID_TEST_ENUM, val);
    zassert_equal(ret, 0);

    size_t encoded_len = enc[0].payload - buf;
    zcbor_state_t dec[2];
    zcbor_new_decode_state(dec, ARRAY_SIZE(dec), buf, encoded_len, 1, NULL, 0);

    data_value_t decoded;
    ret = stow_decode(dec, STOW_ID_TEST_ENUM, &decoded);
    zassert_equal(ret, 0);
    zassert_equal(decoded.data.int_value, Color_GREEN);
    stow_release(STOW_ID_TEST_ENUM, &decoded);
}

// ---------------------------------------------------------------------------
// Suite: stow_string
// ---------------------------------------------------------------------------

ZTEST_SUITE(stow_string, NULL, NULL, reset_stow, NULL, NULL);

ZTEST(stow_string, test_get_default)
{
    data_value_t value;
    int ret = stow_get(AUTH_ANY, STOW_ID_TEST_STRING, &value);
    zassert_equal(ret, 0);
    zassert_str_equal(value.data.string_value, "hello");
    stow_release(STOW_ID_TEST_STRING, &value);
}

ZTEST(stow_string, test_set_and_get)
{
    data_value_t val = { .type = STOW_ITEM_TYPE_STRING, .data.string_value = "world" };
    int ret = stow_set(AUTH_SESSION, STOW_ID_TEST_STRING, val);
    zassert_equal(ret, 0);

    data_value_t got;
    ret = stow_get(AUTH_ANY, STOW_ID_TEST_STRING, &got);
    zassert_equal(ret, 0);
    zassert_str_equal(got.data.string_value, "world");
    stow_release(STOW_ID_TEST_STRING, &got);
}

ZTEST(stow_string, test_set_too_short)
{
    data_value_t val = { .type = STOW_ITEM_TYPE_STRING, .data.string_value = "" };
    int ret = stow_set(AUTH_SESSION, STOW_ID_TEST_STRING, val);
    zassert_equal(ret, -EINVAL);
}

ZTEST(stow_string, test_set_too_long)
{
    data_value_t val = {
        .type = STOW_ITEM_TYPE_STRING,
        .data.string_value = "this_string_is_way_too_long_for_the_constraint_maximum",
    };
    int ret = stow_set(AUTH_SESSION, STOW_ID_TEST_STRING, val);
    zassert_equal(ret, -EINVAL);
}

ZTEST(stow_string, test_encode_decode)
{
    uint8_t buf[64];
    zcbor_state_t enc[2];
    zcbor_new_encode_state(enc, ARRAY_SIZE(enc), buf, sizeof(buf), 1);

    data_value_t val = { .type = STOW_ITEM_TYPE_STRING, .data.string_value = "test" };
    int ret = stow_encode(enc, STOW_ID_TEST_STRING, val);
    zassert_equal(ret, 0);

    size_t encoded_len = enc[0].payload - buf;
    zcbor_state_t dec[2];
    zcbor_new_decode_state(dec, ARRAY_SIZE(dec), buf, encoded_len, 1, NULL, 0);

    data_value_t decoded;
    ret = stow_decode(dec, STOW_ID_TEST_STRING, &decoded);
    zassert_equal(ret, 0);
    zassert_mem_equal(decoded.data.string_value, "test", 4);
    stow_release(STOW_ID_TEST_STRING, &decoded);
}

// ---------------------------------------------------------------------------
// Suite: stow_byte_array
// ---------------------------------------------------------------------------

ZTEST_SUITE(stow_byte_array, NULL, NULL, reset_stow, NULL, NULL);

ZTEST(stow_byte_array, test_get_default)
{
    data_value_t value;
    int ret = stow_get(AUTH_ANY, STOW_ID_TEST_BYTES, &value);
    zassert_equal(ret, 0);
    zassert_equal(value.type, STOW_ITEM_TYPE_BYTE_ARRAY);
    zassert_equal(value.data.buffer_value->len, 4);
    zassert_equal(value.data.buffer_value->buf[0], 0x01);
    zassert_equal(value.data.buffer_value->buf[3], 0x04);
    stow_release(STOW_ID_TEST_BYTES, &value);
}

ZTEST(stow_byte_array, test_set_and_get)
{
    uint8_t raw_buf[sizeof(buffer_t) + 4];
    buffer_t* buf = (buffer_t*)raw_buf;
    buf->len = 4;
    buf->buf[0] = 0xAA;
    buf->buf[1] = 0xBB;
    buf->buf[2] = 0xCC;
    buf->buf[3] = 0xDD;

    data_value_t val = { .type = STOW_ITEM_TYPE_BYTE_ARRAY, .data.buffer_value = buf };
    int ret = stow_set(AUTH_ANY, STOW_ID_TEST_BYTES, val);
    zassert_equal(ret, 0);

    data_value_t got;
    ret = stow_get(AUTH_ANY, STOW_ID_TEST_BYTES, &got);
    zassert_equal(ret, 0);
    zassert_equal(got.data.buffer_value->len, 4);
    zassert_equal(got.data.buffer_value->buf[0], 0xAA);
    zassert_equal(got.data.buffer_value->buf[3], 0xDD);
    stow_release(STOW_ID_TEST_BYTES, &got);
}

ZTEST(stow_byte_array, test_wrong_length)
{
    uint8_t raw_buf[sizeof(buffer_t) + 2];
    buffer_t* buf = (buffer_t*)raw_buf;
    buf->len = 2;

    data_value_t val = { .type = STOW_ITEM_TYPE_BYTE_ARRAY, .data.buffer_value = buf };
    int ret = stow_set(AUTH_ANY, STOW_ID_TEST_BYTES, val);
    zassert_equal(ret, -EINVAL);
}

ZTEST(stow_byte_array, test_encode_decode)
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

    data_value_t val = { .type = STOW_ITEM_TYPE_BYTE_ARRAY, .data.buffer_value = buf };
    int ret = stow_encode(enc, STOW_ID_TEST_BYTES, val);
    zassert_equal(ret, 0);

    size_t encoded_len = enc[0].payload - cbor_buf;
    zcbor_state_t dec[2];
    zcbor_new_decode_state(dec, ARRAY_SIZE(dec), cbor_buf, encoded_len, 1, NULL, 0);

    data_value_t decoded;
    ret = stow_decode(dec, STOW_ID_TEST_BYTES, &decoded);
    zassert_equal(ret, 0);
    zassert_equal(decoded.data.buffer_value->len, 4);
    zassert_equal(decoded.data.buffer_value->buf[0], 0x10);
    stow_release(STOW_ID_TEST_BYTES, &decoded);
}

// ---------------------------------------------------------------------------
// Suite: stow_buffer
// ---------------------------------------------------------------------------

ZTEST_SUITE(stow_buffer, NULL, NULL, reset_stow, NULL, NULL);

ZTEST(stow_buffer, test_get_default)
{
    data_value_t value;
    int ret = stow_get(AUTH_ANY, STOW_ID_TEST_BUFFER, &value);
    zassert_equal(ret, 0);
    zassert_equal(value.type, STOW_ITEM_TYPE_BUFFER);
    zassert_equal(value.data.buffer_value->len, 0);
    stow_release(STOW_ID_TEST_BUFFER, &value);
}

ZTEST(stow_buffer, test_set_and_get)
{
    uint8_t raw_buf[sizeof(buffer_t) + 8];
    buffer_t* buf = (buffer_t*)raw_buf;
    buf->len = 8;
    memset(buf->buf, 0x5A, 8);

    data_value_t val = { .type = STOW_ITEM_TYPE_BUFFER, .data.buffer_value = buf };
    int ret = stow_set(AUTH_ANY, STOW_ID_TEST_BUFFER, val);
    zassert_equal(ret, 0);

    data_value_t got;
    ret = stow_get(AUTH_ANY, STOW_ID_TEST_BUFFER, &got);
    zassert_equal(ret, 0);
    zassert_equal(got.data.buffer_value->len, 8);
    zassert_equal(got.data.buffer_value->buf[0], 0x5A);
    stow_release(STOW_ID_TEST_BUFFER, &got);
}

ZTEST(stow_buffer, test_set_exceeds_max)
{
    uint8_t raw_buf[sizeof(buffer_t) + 65];
    buffer_t* buf = (buffer_t*)raw_buf;
    buf->len = 65;

    data_value_t val = { .type = STOW_ITEM_TYPE_BUFFER, .data.buffer_value = buf };
    int ret = stow_set(AUTH_ANY, STOW_ID_TEST_BUFFER, val);
    zassert_equal(ret, -EINVAL);
}

ZTEST(stow_buffer, test_encode_decode)
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

    data_value_t val = { .type = STOW_ITEM_TYPE_BUFFER, .data.buffer_value = buf };
    int ret = stow_encode(enc, STOW_ID_TEST_BUFFER, val);
    zassert_equal(ret, 0);

    size_t encoded_len = enc[0].payload - cbor_buf;
    zcbor_state_t dec[2];
    zcbor_new_decode_state(dec, ARRAY_SIZE(dec), cbor_buf, encoded_len, 1, NULL, 0);

    data_value_t decoded;
    ret = stow_decode(dec, STOW_ID_TEST_BUFFER, &decoded);
    zassert_equal(ret, 0);
    zassert_equal(decoded.data.buffer_value->len, 3);
    zassert_equal(decoded.data.buffer_value->buf[1], 0xFE);
    stow_release(STOW_ID_TEST_BUFFER, &decoded);
}

// ---------------------------------------------------------------------------
// Suite: stow_access
// ---------------------------------------------------------------------------

ZTEST_SUITE(stow_access, NULL, NULL, reset_stow, NULL, NULL);

ZTEST(stow_access, test_write_requires_session)
{
    data_value_t val = { .type = STOW_ITEM_TYPE_STRING, .data.string_value = "newname" };
    int ret = stow_set(AUTH_ANY, STOW_ID_TEST_STRING, val);
    zassert_equal(ret, -EACCES);
}

ZTEST(stow_access, test_write_with_session_succeeds)
{
    data_value_t val = { .type = STOW_ITEM_TYPE_STRING, .data.string_value = "newname" };
    int ret = stow_set(AUTH_SESSION, STOW_ID_TEST_STRING, val);
    zassert_equal(ret, 0);
}

ZTEST(stow_access, test_read_only_item_cannot_be_written_by_session)
{
    data_value_t val = { .type = STOW_ITEM_TYPE_INT, .data.int_value = 10 };
    int ret = stow_set(AUTH_SESSION, STOW_ID_TEST_READ_ONLY, val);
    zassert_equal(ret, -EACCES);
}

ZTEST(stow_access, test_internal_can_write_internal_item)
{
    data_value_t val = { .type = STOW_ITEM_TYPE_INT, .data.int_value = 10 };
    int ret = stow_set(AUTH_INTERNAL, STOW_ID_TEST_READ_ONLY, val);
    zassert_equal(ret, 0);
}

ZTEST(stow_access, test_tofu_can_be_written_from_default)
{
    data_value_t val = { .type = STOW_ITEM_TYPE_STRING, .data.string_value = "myvalue" };
    int ret = stow_set(AUTH_SESSION, STOW_ID_TEST_TOFU, val);
    zassert_equal(ret, 0);
}

ZTEST(stow_access, test_tofu_cannot_be_overwritten)
{
    data_value_t val1 = { .type = STOW_ITEM_TYPE_STRING, .data.string_value = "first" };
    int ret = stow_set(AUTH_SESSION, STOW_ID_TEST_TOFU, val1);
    zassert_equal(ret, 0);

    data_value_t val2 = { .type = STOW_ITEM_TYPE_STRING, .data.string_value = "second" };
    ret = stow_set(AUTH_SESSION, STOW_ID_TEST_TOFU, val2);
    zassert_equal(ret, -EACCES);
}

ZTEST(stow_access, test_invalid_id)
{
    zassert_false(stow_is_id_valid(STOW_ID_COUNT));
    zassert_false(stow_is_id_valid(0xFFFFFFFF));
}

ZTEST(stow_access, test_valid_ids)
{
    for (uint32_t id = 0; id < STOW_ID_COUNT; id++)
    {
        zassert_true(stow_is_id_valid(id));
    }
}

// ---------------------------------------------------------------------------
// Suite: stow_subscribe
// ---------------------------------------------------------------------------

static bool g_callback_called = false;
static int g_callback_handle_id = -1;
static int g_callback_copy_value = -1;

static void handle_callback(event_t* event)
{
    struct stow_update_event_payload* payload = (struct stow_update_event_payload*)event->data.buf;
    g_callback_called = true;
    g_callback_handle_id = (int)payload->metadata->id;
}

static void copy_callback(event_t* event)
{
    struct stow_update_event_payload* payload = (struct stow_update_event_payload*)event->data.buf;
    g_callback_called = true;
    g_callback_copy_value = payload->value_copy.data.int_value;
}

static void reset_stow_and_flags(void* fixture)
{
    (void)fixture;
    stow_init();
    g_callback_called = false;
    g_callback_handle_id = -1;
    g_callback_copy_value = -1;
}

ZTEST_SUITE(stow_subscribe, NULL, NULL, reset_stow_and_flags, NULL, NULL);

ZTEST(stow_subscribe, test_handle_subscription)
{
    struct stow_subscription sub = {
        .mode = STOW_SUBSCRIPTION_HANDLE,
        .cb = handle_callback,
    };
    int ret = stow_subscribe(AUTH_ANY, STOW_ID_TEST_INT, &sub);
    zassert_equal(ret, 0);

    data_value_t val = { .type = STOW_ITEM_TYPE_INT, .data.int_value = 7 };
    stow_set(AUTH_ANY, STOW_ID_TEST_INT, val);

    zassert_true(g_callback_called);
    zassert_equal(g_callback_handle_id, (int)STOW_ID_TEST_INT);

    ret = stow_unsubscribe(STOW_ID_TEST_INT, &sub);
    zassert_equal(ret, 0);
}

ZTEST(stow_subscribe, test_copy_subscription)
{
    struct stow_subscription sub = {
        .mode = STOW_SUBSCRIPTION_COPY,
        .cb = copy_callback,
    };
    int ret = stow_subscribe(AUTH_ANY, STOW_ID_TEST_INT, &sub);
    zassert_equal(ret, 0);

    data_value_t val = { .type = STOW_ITEM_TYPE_INT, .data.int_value = 42 };
    stow_set(AUTH_ANY, STOW_ID_TEST_INT, val);

    zassert_true(g_callback_called);
    zassert_equal(g_callback_copy_value, 42);

    stow_unsubscribe(STOW_ID_TEST_INT, &sub);
}

ZTEST(stow_subscribe, test_duplicate_subscription)
{
    struct stow_subscription sub = {
        .mode = STOW_SUBSCRIPTION_HANDLE,
        .cb = handle_callback,
    };
    int ret = stow_subscribe(AUTH_ANY, STOW_ID_TEST_INT, &sub);
    zassert_equal(ret, 0);

    ret = stow_subscribe(AUTH_ANY, STOW_ID_TEST_INT, &sub);
    zassert_equal(ret, -EALREADY);

    stow_unsubscribe(STOW_ID_TEST_INT, &sub);
}

ZTEST(stow_subscribe, test_unsubscribe_not_subscribed)
{
    struct stow_subscription sub = {
        .mode = STOW_SUBSCRIPTION_HANDLE,
        .cb = handle_callback,
    };
    int ret = stow_unsubscribe(STOW_ID_TEST_INT, &sub);
    zassert_equal(ret, -ENOENT);
}

ZTEST(stow_subscribe, test_no_callback_after_unsubscribe)
{
    struct stow_subscription sub = {
        .mode = STOW_SUBSCRIPTION_HANDLE,
        .cb = handle_callback,
    };
    stow_subscribe(AUTH_ANY, STOW_ID_TEST_INT, &sub);
    stow_unsubscribe(STOW_ID_TEST_INT, &sub);

    data_value_t val = { .type = STOW_ITEM_TYPE_INT, .data.int_value = 5 };
    stow_set(AUTH_ANY, STOW_ID_TEST_INT, val);

    zassert_false(g_callback_called);
}

// ---------------------------------------------------------------------------
// Suite: stow_describe
// ---------------------------------------------------------------------------

ZTEST_SUITE(stow_describe, NULL, NULL, reset_stow, NULL, NULL);

ZTEST(stow_describe, test_full_describe_succeeds)
{
    uint8_t buf[4096];
    zcbor_state_t enc[4];
    zcbor_new_encode_state(enc, ARRAY_SIZE(enc), buf, sizeof(buf), STOW_ID_COUNT);

    struct stow_describe_state state;
    stow_describe_start(&state);

    int ret = stow_describe(&state, enc);
    zassert_equal(ret, 0);
    zassert_equal(state.current_id, STOW_ID_COUNT);
}

ZTEST(stow_describe, test_tiny_buffer_returns_enomem)
{
    uint8_t buf[1];
    zcbor_state_t enc[4];
    zcbor_new_encode_state(enc, ARRAY_SIZE(enc), buf, sizeof(buf), STOW_ID_COUNT);

    struct stow_describe_state state;
    stow_describe_start(&state);

    int ret = stow_describe(&state, enc);
    zassert_equal(ret, -ENOMEM);
    zassert_equal(state.current_id, 0);
}

ZTEST(stow_describe, test_rollback_leaves_no_partial_data)
{
    uint8_t buf[8];
    zcbor_state_t enc[4];
    zcbor_new_encode_state(enc, ARRAY_SIZE(enc), buf, sizeof(buf), STOW_ID_COUNT);

    const uint8_t* payload_before = enc[0].payload;

    struct stow_describe_state state;
    stow_describe_start(&state);
    stow_describe(&state, enc);

    zassert_equal_ptr(enc[0].payload, payload_before);
}

ZTEST(stow_describe, test_chunked_describe_covers_all_items)
{
    struct stow_describe_state state;
    stow_describe_start(&state);

    uint32_t items_encoded = 0;

    while (state.current_id < STOW_ID_COUNT)
    {
        uint8_t buf[1024];
        zcbor_state_t enc[4];
        zcbor_new_encode_state(enc, ARRAY_SIZE(enc), buf, sizeof(buf), STOW_ID_COUNT);

        uint32_t id_before = state.current_id;
        int ret = stow_describe(&state, enc);

        if (ret == 0)
        {
            items_encoded += STOW_ID_COUNT - id_before;
            break;
        }
        else if (ret == -ENOMEM)
        {
            uint32_t newly_encoded_count = state.current_id - id_before;
            zassert_true(newly_encoded_count > 0);
            items_encoded += newly_encoded_count;
        }
        else
        {
            zassert_unreachable("Unexpected return value from stow_describe");
        }
    }

    zassert_equal(items_encoded, STOW_ID_COUNT);
}

#define EXPECT_KEY(dec_state, expected_str)                                                                          \
    do                                                                                                               \
    {                                                                                                                \
        struct zcbor_string key;                                                                                     \
        zassert_true(zcbor_tstr_decode(dec_state, &key), "Failed to decode key");                                    \
        zassert_equal(key.len, strlen(expected_str), "Key length mismatch for %s", expected_str);                    \
        zassert_mem_equal(key.value, expected_str, strlen(expected_str), "Key mismatch, expected %s", expected_str); \
    } while (0)

ZTEST(stow_describe, test_describe_encoding)
{
    uint8_t buf[256];
    zcbor_state_t enc[4];
    zcbor_new_encode_state(enc, ARRAY_SIZE(enc), buf, sizeof(buf), STOW_ID_COUNT);

    struct stow_describe_state state;
    stow_describe_start(&state);

    int ret = stow_describe(&state, enc);
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

    // categories (Skip the value)
    EXPECT_KEY(dec, "categories");
    zassert_true(zcbor_any_skip(dec, NULL), "Failed to skip 'categories' value");

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

// ---------------------------------------------------------------------------
// Suite: stow_struct
// ---------------------------------------------------------------------------

ZTEST_SUITE(stow_struct, NULL, NULL, reset_stow, NULL, NULL);

ZTEST(stow_struct, test_get_default)
{
    data_value_t value;
    int ret = stow_get(AUTH_ANY, STOW_ID_TEST_STRUCT_ITEM, &value);
    zassert_equal(ret, 0);
    zassert_equal(value.type, STOW_ITEM_TYPE_STRUCT);

    TestStruct_t* s = (TestStruct_t*)value.data.raw_value;
    zassert_equal(s->int_field, 7);
    zassert_equal(s->buf_field->len, 0);

    stow_release(STOW_ID_TEST_STRUCT_ITEM, &value);
}

ZTEST(stow_struct, test_set_and_get_scalar_field)
{
    // Build a TestStruct_t value to set
    uint8_t empty_buf_storage[sizeof(buffer_t)];
    buffer_t* empty_buf = (buffer_t*)empty_buf_storage;
    empty_buf->len = 0;

    TestStruct_t src = {
        .int_field = 42,
        .float_field = 0.0f,
        .enum_field = Color_BLUE,
        .string_field = "Hello",
        .inline_bytes_field = { .len = 3, .buf = { 0x01, 0x02, 0x03 } },
        .buf_field = empty_buf,
    };

    data_value_t val = {
        .type = STOW_ITEM_TYPE_STRUCT,
        .data.raw_value = &src,
    };
    int ret = stow_set(AUTH_ANY, STOW_ID_TEST_STRUCT_ITEM, val);
    zassert_equal(ret, 0);

    data_value_t got;
    ret = stow_get(AUTH_ANY, STOW_ID_TEST_STRUCT_ITEM, &got);
    zassert_equal(ret, 0);
    TestStruct_t* result = (TestStruct_t*)got.data.raw_value;
    zassert_equal(result->int_field, 42);
    stow_release(STOW_ID_TEST_STRUCT_ITEM, &got);
}

ZTEST(stow_struct, test_set_and_get_buffer_field)
{
    uint8_t field_buf_storage[sizeof(buffer_t) + 4];
    buffer_t* field_buf = (buffer_t*)field_buf_storage;
    field_buf->len = 4;
    field_buf->buf[0] = 0xDE;
    field_buf->buf[1] = 0xAD;
    field_buf->buf[2] = 0xBE;
    field_buf->buf[3] = 0xEF;

    TestStruct_t src = {
        .int_field = 0,
        .float_field = 0.0f,
        .enum_field = Color_BLUE,
        .string_field = "Hi",
        .bytes_field = { 0 },
        .buf_field = field_buf,
    };

    data_value_t val = {
        .type = STOW_ITEM_TYPE_STRUCT,
        .data.raw_value = &src,
    };
    int ret = stow_set(AUTH_ANY, STOW_ID_TEST_STRUCT_ITEM, val);
    zassert_equal(ret, 0);

    data_value_t got;
    ret = stow_get(AUTH_ANY, STOW_ID_TEST_STRUCT_ITEM, &got);
    zassert_equal(ret, 0);
    TestStruct_t* result = (TestStruct_t*)got.data.raw_value;
    zassert_equal(result->buf_field->len, 4);
    zassert_equal(result->buf_field->buf[0], 0xDE);
    zassert_equal(result->buf_field->buf[3], 0xEF);
    stow_release(STOW_ID_TEST_STRUCT_ITEM, &got);
}

ZTEST(stow_struct, test_validate_buffer_field_too_large)
{
    uint8_t field_buf_storage[sizeof(buffer_t) + 17];
    buffer_t* field_buf = (buffer_t*)field_buf_storage;
    field_buf->len = 17;  // max is 16

    TestStruct_t src = {
        .int_field = 0,
        .float_field = 0.0f,
        .enum_field = Color_BLUE,
        .string_field = "Hello",
        .bytes_field = { 0 },
        .buf_field = field_buf,
    };

    data_value_t val = {
        .type = STOW_ITEM_TYPE_STRUCT,
        .data.raw_value = &src,
    };
    int ret = stow_set(AUTH_ANY, STOW_ID_TEST_STRUCT_ITEM, val);
    zassert_equal(ret, -EINVAL);
}

ZTEST(stow_struct, test_validate_int_field_out_of_range)
{
    uint8_t empty_buf_storage[sizeof(buffer_t)];
    buffer_t* empty_buf = (buffer_t*)empty_buf_storage;
    empty_buf->len = 0;

    TestStruct_t src = {
        .int_field = 256,  // max is 255
        .float_field = 0.0f,
        .enum_field = Color_BLUE,
        .string_field = "Hi",
        .bytes_field = { 0 },
        .buf_field = empty_buf,
    };

    data_value_t val = {
        .type = STOW_ITEM_TYPE_STRUCT,
        .data.raw_value = &src,
    };
    int ret = stow_set(AUTH_ANY, STOW_ID_TEST_STRUCT_ITEM, val);
    zassert_equal(ret, -EINVAL);
}

ZTEST(stow_struct, test_encode_decode)
{
    uint8_t field_buf_storage[sizeof(buffer_t) + 2];
    buffer_t* field_buf = (buffer_t*)field_buf_storage;
    field_buf->len = 2;
    field_buf->buf[0] = 0xAB;
    field_buf->buf[1] = 0xCD;

    TestStruct_t src = {
        .int_field = 99,
        .float_field = 0.0f,
        .enum_field = Color_BLUE,
        .string_field = "hi",
        .bytes_field = { 0 },
        .buf_field = field_buf,
    };

    data_value_t val = {
        .type = STOW_ITEM_TYPE_STRUCT,
        .data.raw_value = &src,
    };

    uint8_t cbor_buf[128];
    zcbor_state_t enc[4];
    zcbor_new_encode_state(enc, ARRAY_SIZE(enc), cbor_buf, sizeof(cbor_buf), 1);

    int ret = stow_encode(enc, STOW_ID_TEST_STRUCT_ITEM, val);
    zassert_equal(ret, 0);

    size_t encoded_len = enc[0].payload - cbor_buf;
    zcbor_state_t dec[4];
    zcbor_new_decode_state(dec, ARRAY_SIZE(dec), cbor_buf, encoded_len, 1, NULL, 0);

    data_value_t decoded;
    ret = stow_decode(dec, STOW_ID_TEST_STRUCT_ITEM, &decoded);
    zassert_equal(ret, 0);
    zassert_equal(decoded.type, STOW_ITEM_TYPE_STRUCT);

    TestStruct_t* result = (TestStruct_t*)decoded.data.raw_value;
    zassert_equal(result->int_field, 99);
    zassert_equal(result->buf_field->len, 2);
    zassert_equal(result->buf_field->buf[0], 0xAB);
    zassert_equal(result->buf_field->buf[1], 0xCD);

    stow_release(STOW_ID_TEST_STRUCT_ITEM, &decoded);
}

// ---------------------------------------------------------------------------
// Suite: stow_nested_struct
// ---------------------------------------------------------------------------

ZTEST_SUITE(stow_nested_struct, NULL, NULL, reset_stow, NULL, NULL);

ZTEST(stow_nested_struct, test_get_default)
{
    data_value_t value;
    int ret = stow_get(AUTH_ANY, STOW_ID_TEST_NESTED_STRUCT_ITEM, &value);
    zassert_equal(ret, 0);
    zassert_equal(value.type, STOW_ITEM_TYPE_STRUCT);

    NestedStruct_t* s = (NestedStruct_t*)value.data.raw_value;
    zassert_equal(s->flag, 0);
    zassert_not_null(s->inner);
    zassert_equal(s->inner->int_field, 0);
    zassert_equal(s->inner->buf_field->len, 0);

    stow_release(STOW_ID_TEST_NESTED_STRUCT_ITEM, &value);
}

ZTEST(stow_nested_struct, test_set_get_roundtrip)
{
    uint8_t inner_buf_storage[sizeof(buffer_t) + 2];
    buffer_t* inner_buf = (buffer_t*)inner_buf_storage;
    inner_buf->len = 2;
    inner_buf->buf[0] = 0xAA;
    inner_buf->buf[1] = 0xBB;

    TestStruct_t inner_src = {
        .int_field = 55,
        .float_field = 0.0f,
        .enum_field = Color_BLUE,
        .string_field = "hi",
        .bytes_field = { 0 },
        .buf_field = inner_buf,
    };
    NestedStruct_t outer_src = {
        .inner = &inner_src,
        .flag = 1,
    };

    data_value_t val = {
        .type = STOW_ITEM_TYPE_STRUCT,
        .data.raw_value = &outer_src,
    };
    int ret = stow_set(AUTH_ANY, STOW_ID_TEST_NESTED_STRUCT_ITEM, val);
    zassert_equal(ret, 0);

    data_value_t got;
    ret = stow_get(AUTH_ANY, STOW_ID_TEST_NESTED_STRUCT_ITEM, &got);
    zassert_equal(ret, 0);
    NestedStruct_t* result = (NestedStruct_t*)got.data.raw_value;
    zassert_equal(result->flag, 1);
    zassert_not_null(result->inner);
    zassert_equal(result->inner->int_field, 55);
    zassert_equal(result->inner->buf_field->len, 2);
    zassert_equal(result->inner->buf_field->buf[0], 0xAA);
    zassert_equal(result->inner->buf_field->buf[1], 0xBB);
    stow_release(STOW_ID_TEST_NESTED_STRUCT_ITEM, &got);
}

ZTEST(stow_nested_struct, test_release_frees_recursively)
{
    uint32_t used_before = 0;
    uint32_t total = 0;
    mem_get_pool_usage(0, &used_before, &total);

    uint8_t inner_buf_storage[sizeof(buffer_t) + 3];
    buffer_t* inner_buf = (buffer_t*)inner_buf_storage;
    inner_buf->len = 3;
    inner_buf->buf[0] = 0x01;
    inner_buf->buf[1] = 0x02;
    inner_buf->buf[2] = 0x03;

    TestStruct_t inner_src = { .int_field = 10, .buf_field = inner_buf };
    NestedStruct_t outer_src = { .inner = &inner_src, .flag = 0 };

    data_value_t val = { .type = STOW_ITEM_TYPE_STRUCT, .data.raw_value = &outer_src };
    int ret = stow_set(AUTH_ANY, STOW_ID_TEST_NESTED_STRUCT_ITEM, val);
    zassert_equal(ret, 0);

    uint32_t used_after = 0;
    mem_get_pool_usage(0, &used_after, &total);
    zassert_equal(used_after, used_before);

    data_value_t got;
    ret = stow_get(AUTH_ANY, STOW_ID_TEST_NESTED_STRUCT_ITEM, &got);
    zassert_equal(ret, 0);

    used_after = 0;
    mem_get_pool_usage(0, &used_after, &total);
    zassert_equal(used_after, used_before);

    stow_release(STOW_ID_TEST_NESTED_STRUCT_ITEM, &got);

    used_after = 0;
    mem_get_pool_usage(0, &used_after, &total);
    zassert_equal(used_after, used_before);

    // Re-init resets stored value, releasing all heap blocks
    stow_init();

    used_after = 0;
    mem_get_pool_usage(0, &used_after, &total);
    zassert_equal(used_after, used_before);
}

ZTEST(stow_nested_struct, test_encode_decode_roundtrip)
{
    uint8_t inner_buf_storage[sizeof(buffer_t) + 2];
    buffer_t* inner_buf = (buffer_t*)inner_buf_storage;
    inner_buf->len = 2;
    inner_buf->buf[0] = 0xCA;
    inner_buf->buf[1] = 0xFE;

    TestStruct_t inner_src = {
        .int_field = 77,
        .float_field = 0.0f,
        .enum_field = Color_BLUE,
        .string_field = "Test",
        .bytes_field = { 0 },
        .buf_field = inner_buf,
    };
    NestedStruct_t outer_src = { .inner = &inner_src, .flag = 1 };

    data_value_t val = { .type = STOW_ITEM_TYPE_STRUCT, .data.raw_value = &outer_src };

    uint8_t cbor_buf[256];
    zcbor_state_t enc[4];
    zcbor_new_encode_state(enc, ARRAY_SIZE(enc), cbor_buf, sizeof(cbor_buf), 1);

    int ret = stow_encode(enc, STOW_ID_TEST_NESTED_STRUCT_ITEM, val);
    zassert_equal(ret, 0);

    size_t encoded_len = enc[0].payload - cbor_buf;
    zcbor_state_t dec[4];
    zcbor_new_decode_state(dec, ARRAY_SIZE(dec), cbor_buf, encoded_len, 1, NULL, 0);

    data_value_t decoded;
    ret = stow_decode(dec, STOW_ID_TEST_NESTED_STRUCT_ITEM, &decoded);
    zassert_equal(ret, 0);
    zassert_equal(decoded.type, STOW_ITEM_TYPE_STRUCT);

    NestedStruct_t* result = (NestedStruct_t*)decoded.data.raw_value;
    zassert_equal(result->flag, 1);
    zassert_not_null(result->inner);
    zassert_equal(result->inner->int_field, 77);
    zassert_equal(result->inner->buf_field->len, 2);
    zassert_equal(result->inner->buf_field->buf[0], 0xCA);
    zassert_equal(result->inner->buf_field->buf[1], 0xFE);

    stow_release(STOW_ID_TEST_NESTED_STRUCT_ITEM, &decoded);
}
