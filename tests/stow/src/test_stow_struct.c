/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "generated_struct_TestStruct.h"
#include "test_common.h"

ZTEST_SUITE(stow_struct, NULL, NULL, reset_stow, NULL, NULL);

ZTEST(stow_struct, test_get_default)
{
    data_value_t value;
    int ret = stow_get(STOW_ROLE_GUEST, STOW_ID_TEST_STRUCT_ITEM, &value);
    zassert_equal(ret, 0);
    zassert_equal(value.type, STOW_ITEM_TYPE_STRUCT);

    TestStruct_t* s = (TestStruct_t*)value.data.raw_value;
    zassert_equal(s->int_field, 7);
    zassert_equal(s->buf_field->len, 0);

    stow_release(STOW_ID_TEST_STRUCT_ITEM, &value);
}

ZTEST(stow_struct, test_set_and_get_scalar_field)
{
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
    int ret = stow_set(STOW_ROLE_GUEST, STOW_ID_TEST_STRUCT_ITEM, val);
    zassert_equal(ret, 0);

    data_value_t got;
    ret = stow_get(STOW_ROLE_GUEST, STOW_ID_TEST_STRUCT_ITEM, &got);
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
    int ret = stow_set(STOW_ROLE_GUEST, STOW_ID_TEST_STRUCT_ITEM, val);
    zassert_equal(ret, 0);

    data_value_t got;
    ret = stow_get(STOW_ROLE_GUEST, STOW_ID_TEST_STRUCT_ITEM, &got);
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
    int ret = stow_set(STOW_ROLE_GUEST, STOW_ID_TEST_STRUCT_ITEM, val);
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
    int ret = stow_set(STOW_ROLE_GUEST, STOW_ID_TEST_STRUCT_ITEM, val);
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
