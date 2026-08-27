/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "test_common.h"

ZTEST_SUITE(stow_enum, NULL, NULL, reset_stow, NULL, NULL);

ZTEST(stow_enum, test_get_default)
{
    data_value_t value;
    int ret = stow_get(STOW_ID_TEST_ENUM, &value);
    zassert_equal(ret, 0);
    zassert_equal(value.type, STOW_ITEM_TYPE_ENUM);
    zassert_equal(value.data.int_value, Color_RED);
    stow_release(STOW_ID_TEST_ENUM, &value);
}

ZTEST(stow_enum, test_set_valid)
{
    data_value_t val = { .type = STOW_ITEM_TYPE_ENUM, .data.int_value = Color_BLUE };
    int ret = stow_set(STOW_ID_TEST_ENUM, val);
    zassert_equal(ret, 0);

    data_value_t got;
    ret = stow_get(STOW_ID_TEST_ENUM, &got);
    zassert_equal(ret, 0);
    zassert_equal(got.data.int_value, Color_BLUE);
    stow_release(STOW_ID_TEST_ENUM, &got);
}

ZTEST(stow_enum, test_set_invalid)
{
    data_value_t val = { .type = STOW_ITEM_TYPE_ENUM, .data.int_value = 99 };
    int ret = stow_set(STOW_ID_TEST_ENUM, val);
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
