/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "test_common.h"

ZTEST_SUITE(stow_access, NULL, NULL, reset_stow, NULL, NULL);

ZTEST(stow_access, test_external_write_requires_session)
{
    data_value_t val = { .type = STOW_ITEM_TYPE_STRING, .data.string_value = "newname" };
    int ret = stow_set_external(STOW_ROLE_GUEST, STOW_ID_TEST_STRING, val);
    zassert_equal(ret, -EACCES);
}

ZTEST(stow_access, test_external_write_with_session_succeeds)
{
    data_value_t val = { .type = STOW_ITEM_TYPE_STRING, .data.string_value = "newname" };
    int ret = stow_set_external(STOW_ROLE_SESSION, STOW_ID_TEST_STRING, val);
    zassert_equal(ret, 0);
}

ZTEST(stow_access, test_external_read_only_item_cannot_be_written_by_session)
{
    data_value_t val = { .type = STOW_ITEM_TYPE_INT, .data.int_value = 10 };
    int ret = stow_set_external(STOW_ROLE_SESSION, STOW_ID_TEST_READ_ONLY, val);
    zassert_equal(ret, -EACCES);
}

ZTEST(stow_access, test_external_cannot_write_internal_only_item)
{
    data_value_t val = { .type = STOW_ITEM_TYPE_INT, .data.int_value = 10 };
    int ret = stow_set_external(STOW_ROLE_ANY, STOW_ID_TEST_READ_ONLY, val);
    zassert_equal(ret, -EACCES);
}

ZTEST(stow_access, test_internal_write_bypasses_permissions)
{
    data_value_t val = { .type = STOW_ITEM_TYPE_INT, .data.int_value = 10 };
    int ret = stow_set(STOW_ID_TEST_READ_ONLY, val);
    zassert_equal(ret, 0);
}

ZTEST(stow_access, test_external_tofu_can_be_written_from_default)
{
    data_value_t val = { .type = STOW_ITEM_TYPE_STRING, .data.string_value = "myvalue" };
    int ret = stow_set_external(STOW_ROLE_SESSION, STOW_ID_TEST_TOFU, val);
    zassert_equal(ret, 0);
}

ZTEST(stow_access, test_external_tofu_cannot_be_overwritten)
{
    data_value_t val1 = { .type = STOW_ITEM_TYPE_STRING, .data.string_value = "first" };
    int ret = stow_set_external(STOW_ROLE_SESSION, STOW_ID_TEST_TOFU, val1);
    zassert_equal(ret, 0);

    data_value_t val2 = { .type = STOW_ITEM_TYPE_STRING, .data.string_value = "second" };
    ret = stow_set_external(STOW_ROLE_SESSION, STOW_ID_TEST_TOFU, val2);
    zassert_equal(ret, -EACCES);
}

ZTEST(stow_access, test_external_read_allowed_for_read_only_item)
{
    data_value_t val = { 0 };
    int ret = stow_get_external(STOW_ROLE_GUEST, STOW_ID_TEST_READ_ONLY, &val);
    zassert_equal(ret, 0);
    stow_release(STOW_ID_TEST_READ_ONLY, &val);
}

ZTEST(stow_access, test_external_subscribe_allowed_for_read_only_item)
{
    struct stow_subscription sub = { .mode = STOW_SUBSCRIPTION_HANDLE, .cb = NULL };
    int ret = stow_subscribe_external(STOW_ROLE_ANY, STOW_ID_TEST_READ_ONLY, &sub);
    zassert_equal(ret, 0);
    stow_unsubscribe(STOW_ID_TEST_READ_ONLY, &sub);
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
