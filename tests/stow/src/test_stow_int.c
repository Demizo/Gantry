/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "test_common.h"

ZTEST_SUITE(stow_int, NULL, NULL, reset_stow, NULL, NULL);

ZTEST(stow_int, test_get_default)
{
    data_value_t value;
    int ret = stow_get(STOW_ID_TEST_INT, &value);
    zassert_equal(ret, 0);
    zassert_equal(value.type, STOW_ITEM_TYPE_INT);
    zassert_equal(value.data.int_value, 0);
    stow_release(STOW_ID_TEST_INT, &value);
}

ZTEST(stow_int, test_set_and_get)
{
    data_value_t set_value = { .type = STOW_ITEM_TYPE_INT, .data.int_value = 25 };
    int ret = stow_set(STOW_ID_TEST_INT, set_value);
    zassert_equal(ret, 0);

    data_value_t got;
    ret = stow_get(STOW_ID_TEST_INT, &got);
    zassert_equal(ret, 0);
    zassert_equal(got.data.int_value, 25);
    stow_release(STOW_ID_TEST_INT, &got);
}

ZTEST(stow_int, test_set_out_of_range_max)
{
    data_value_t value = { .type = STOW_ITEM_TYPE_INT, .data.int_value = 51 };
    int ret = stow_set(STOW_ID_TEST_INT, value);
    zassert_equal(ret, -EINVAL);
}

ZTEST(stow_int, test_set_out_of_range_min)
{
    data_value_t value = { .type = STOW_ITEM_TYPE_INT, .data.int_value = -51 };
    int ret = stow_set(STOW_ID_TEST_INT, value);
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
