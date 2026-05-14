/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "test_common.h"

ZTEST_SUITE(stow_float, NULL, NULL, reset_stow, NULL, NULL);

ZTEST(stow_float, test_get_default)
{
    data_value_t value;
    int ret = stow_get(STOW_ROLE_GUEST, STOW_ID_TEST_FLOAT, &value);
    zassert_equal(ret, 0);
    zassert_equal(value.type, STOW_ITEM_TYPE_FLOAT);
    zassert_within(value.data.float_value, 1.0f, 0.001f);
    stow_release(STOW_ID_TEST_FLOAT, &value);
}

ZTEST(stow_float, test_set_and_get)
{
    data_value_t set_value = { .type = STOW_ITEM_TYPE_FLOAT, .data.float_value = 5.5f };
    int ret = stow_set(STOW_ROLE_GUEST, STOW_ID_TEST_FLOAT, set_value);
    zassert_equal(ret, 0);

    data_value_t got;
    ret = stow_get(STOW_ROLE_GUEST, STOW_ID_TEST_FLOAT, &got);
    zassert_equal(ret, 0);
    zassert_within(got.data.float_value, 5.5f, 0.001f);
    stow_release(STOW_ID_TEST_FLOAT, &got);
}

ZTEST(stow_float, test_set_out_of_range)
{
    data_value_t value = { .type = STOW_ITEM_TYPE_FLOAT, .data.float_value = 11.0f };
    int ret = stow_set(STOW_ROLE_GUEST, STOW_ID_TEST_FLOAT, value);
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
