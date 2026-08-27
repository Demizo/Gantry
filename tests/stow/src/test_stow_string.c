/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "test_common.h"

ZTEST_SUITE(stow_string, NULL, NULL, reset_stow, NULL, NULL);

ZTEST(stow_string, test_get_default)
{
    data_value_t value;
    int ret = stow_get(STOW_ID_TEST_STRING, &value);
    zassert_equal(ret, 0);
    zassert_str_equal(value.data.string_value, "hello");
    stow_release(STOW_ID_TEST_STRING, &value);
}

ZTEST(stow_string, test_set_and_get)
{
    data_value_t val = { .type = STOW_ITEM_TYPE_STRING, .data.string_value = "world" };
    int ret = stow_set(STOW_ID_TEST_STRING, val);
    zassert_equal(ret, 0);

    data_value_t got;
    ret = stow_get(STOW_ID_TEST_STRING, &got);
    zassert_equal(ret, 0);
    zassert_str_equal(got.data.string_value, "world");
    stow_release(STOW_ID_TEST_STRING, &got);
}

ZTEST(stow_string, test_set_too_short)
{
    data_value_t val = { .type = STOW_ITEM_TYPE_STRING, .data.string_value = "" };
    int ret = stow_set(STOW_ID_TEST_STRING, val);
    zassert_equal(ret, -EINVAL);
}

ZTEST(stow_string, test_set_too_long)
{
    data_value_t val = {
        .type = STOW_ITEM_TYPE_STRING,
        .data.string_value = "this_string_is_way_too_long_for_the_constraint_maximum",
    };
    int ret = stow_set(STOW_ID_TEST_STRING, val);
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
