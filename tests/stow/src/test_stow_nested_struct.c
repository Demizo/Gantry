/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "generated_struct_NestedStruct.h"
#include "generated_struct_TestStruct.h"
#include "test_common.h"

ZTEST_SUITE(stow_nested_struct, NULL, NULL, reset_stow, NULL, NULL);

ZTEST(stow_nested_struct, test_get_default)
{
    data_value_t value;
    int ret = stow_get(STOW_ID_TEST_NESTED_STRUCT_ITEM, &value);
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
    int ret = stow_set(STOW_ID_TEST_NESTED_STRUCT_ITEM, val);
    zassert_equal(ret, 0);

    data_value_t got;
    ret = stow_get(STOW_ID_TEST_NESTED_STRUCT_ITEM, &got);
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
    int ret = stow_set(STOW_ID_TEST_NESTED_STRUCT_ITEM, val);
    zassert_equal(ret, 0);

    uint32_t used_after = 0;
    mem_get_pool_usage(0, &used_after, &total);
    zassert_equal(used_after, used_before);

    data_value_t got;
    ret = stow_get(STOW_ID_TEST_NESTED_STRUCT_ITEM, &got);
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
