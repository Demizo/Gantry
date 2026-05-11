/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "test_common.h"

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
