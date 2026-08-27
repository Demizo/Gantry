/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "test_common.h"

ZTEST_SUITE(stow_byte_array, NULL, NULL, reset_stow, NULL, NULL);

ZTEST(stow_byte_array, test_get_default)
{
    data_value_t value;
    int ret = stow_get(STOW_ID_TEST_BYTES, &value);
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
    int ret = stow_set(STOW_ID_TEST_BYTES, val);
    zassert_equal(ret, 0);

    data_value_t got;
    ret = stow_get(STOW_ID_TEST_BYTES, &got);
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
    int ret = stow_set(STOW_ID_TEST_BYTES, val);
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
