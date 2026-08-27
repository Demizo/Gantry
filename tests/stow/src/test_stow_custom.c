/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gantry/error.h>

#include "test_common.h"

static int g_backing_value;
static int g_set_calls;
static int g_get_calls;
static int g_forced_set_error;
static const struct stow_item_const_metadata* g_last_set_item;
static const struct stow_item_const_metadata* g_last_get_item;
static int g_getter_only_calls;
static int g_setter_only_calls;
static int g_validate_calls;
static bool g_validator_rejects;

int test_custom_setter(const struct stow_item_const_metadata* item, data_value_t value)
{
    g_set_calls++;
    g_last_set_item = item;
    if (g_forced_set_error != SUCCESS)
    {
        return g_forced_set_error;
    }
    g_backing_value = value.data.int_value;
    return SUCCESS;
}

int test_custom_getter(const struct stow_item_const_metadata* item, data_value_t* out_value)
{
    g_get_calls++;
    g_last_get_item = item;
    out_value->type = STOW_ITEM_TYPE_INT;
    out_value->data.int_value = g_backing_value;
    return SUCCESS;
}

/*
 * Getter-only callback: trips a flag, then delegates to the default
 * interface so the value read comes from item storage.
 */
int test_getter_only(const struct stow_item_const_metadata* item, data_value_t* out_value)
{
    g_getter_only_calls++;
    return item->interface->get(item->value_ptr, out_value);
}

/*
 * Setter-only callback: trips a flag, then delegates to the default
 * interface so the written value lands in item storage and a subsequent
 * default get retrieves it.
 */
int test_setter_only(const struct stow_item_const_metadata* item, data_value_t value)
{
    g_setter_only_calls++;
    item->interface->set(item->value_ptr, value);
    return SUCCESS;
}

/*
 * Custom validator: rejects odd values, or unconditionally rejects when g_validator_rejects is set.
 */
bool test_custom_validator(const struct stow_item_const_metadata* item, data_value_t value)
{
    (void)item;
    g_validate_calls++;
    if (g_validator_rejects)
    {
        return false;
    }
    return (value.data.int_value % 2) == 0;
}

static void reset_custom(void* fixture)
{
    (void)fixture;
    g_backing_value = 0;
    g_set_calls = 0;
    g_get_calls = 0;
    g_forced_set_error = SUCCESS;
    g_last_set_item = NULL;
    g_last_get_item = NULL;
    g_getter_only_calls = 0;
    g_setter_only_calls = 0;
    g_validate_calls = 0;
    g_validator_rejects = false;
    stow_init();
}

ZTEST_SUITE(stow_custom, NULL, NULL, reset_custom, NULL, NULL);

ZTEST(stow_custom, test_custom_set_invokes_callback)
{
    data_value_t value = { .type = STOW_ITEM_TYPE_INT, .data.int_value = 42 };
    int ret = stow_set(STOW_ID_TEST_CUSTOM_INT, value);
    zassert_equal(ret, SUCCESS);
    zassert_equal(g_set_calls, 1);
    zassert_equal(g_backing_value, 42);
    zassert_not_null(g_last_set_item);
    zassert_equal(g_last_set_item->id, STOW_ID_TEST_CUSTOM_INT);
}

ZTEST(stow_custom, test_custom_get_invokes_callback)
{
    g_backing_value = 17;
    data_value_t got;
    int ret = stow_get(STOW_ID_TEST_CUSTOM_INT, &got);
    zassert_equal(ret, SUCCESS);
    zassert_equal(g_get_calls, 1);
    zassert_equal(got.type, STOW_ITEM_TYPE_INT);
    zassert_equal(got.data.int_value, 17);
    zassert_not_null(g_last_get_item);
    zassert_equal(g_last_get_item->id, STOW_ID_TEST_CUSTOM_INT);
    stow_release(STOW_ID_TEST_CUSTOM_INT, &got);
}

ZTEST(stow_custom, test_custom_set_skipped_on_validation_failure)
{
    data_value_t value = { .type = STOW_ITEM_TYPE_INT, .data.int_value = 999 };
    int ret = stow_set(STOW_ID_TEST_CUSTOM_INT, value);
    zassert_equal(ret, -EINVAL);
    zassert_equal(g_set_calls, 0);
    zassert_equal(g_backing_value, 0);
}

ZTEST(stow_custom, test_custom_set_skipped_on_permission_failure)
{
    data_value_t value = { .type = STOW_ITEM_TYPE_INT, .data.int_value = 10 };
    int ret = stow_set_external(STOW_ROLE_ANY, STOW_ID_TEST_CUSTOM_READ_ONLY, value);
    zassert_equal(ret, -EACCES);
    zassert_equal(g_set_calls, 0);
}

ZTEST(stow_custom, test_custom_set_propagates_error)
{
    g_forced_set_error = -EIO;
    data_value_t value = { .type = STOW_ITEM_TYPE_INT, .data.int_value = 5 };
    int ret = stow_set(STOW_ID_TEST_CUSTOM_INT, value);
    zassert_equal(ret, -EIO);
    zassert_equal(g_set_calls, 1);
    zassert_equal(g_backing_value, 0);
}

ZTEST(stow_custom, test_getter_only_set_uses_interface_storage)
{
    data_value_t value = { .type = STOW_ITEM_TYPE_INT, .data.int_value = 33 };
    int ret = stow_set(STOW_ID_TEST_CUSTOM_GETTER_ONLY, value);
    zassert_equal(ret, SUCCESS);
    zassert_equal(g_getter_only_calls, 0);
    zassert_equal(g_set_calls, 0);

    data_value_t got;
    ret = stow_get(STOW_ID_TEST_CUSTOM_GETTER_ONLY, &got);
    zassert_equal(ret, SUCCESS);
    zassert_equal(g_getter_only_calls, 1);
    zassert_equal(got.type, STOW_ITEM_TYPE_INT);
    zassert_equal(got.data.int_value, 33);
    stow_release(STOW_ID_TEST_CUSTOM_GETTER_ONLY, &got);
}

ZTEST(stow_custom, test_setter_only_uses_interface_storage)
{
    data_value_t value = { .type = STOW_ITEM_TYPE_INT, .data.int_value = 77 };
    int ret = stow_set(STOW_ID_TEST_CUSTOM_SETTER_ONLY, value);
    zassert_equal(ret, SUCCESS);
    zassert_equal(g_setter_only_calls, 1);

    data_value_t got;
    ret = stow_get(STOW_ID_TEST_CUSTOM_SETTER_ONLY, &got);
    zassert_equal(ret, SUCCESS);
    zassert_equal(g_get_calls, 0);
    zassert_equal(got.type, STOW_ITEM_TYPE_INT);
    zassert_equal(got.data.int_value, 77);
    stow_release(STOW_ID_TEST_CUSTOM_SETTER_ONLY, &got);
}

ZTEST(stow_custom, test_setter_only_validation_still_runs)
{
    /* Out-of-range value must be rejected by validation before custom_set fires. */
    data_value_t value = { .type = STOW_ITEM_TYPE_INT, .data.int_value = 500 };
    int ret = stow_set(STOW_ID_TEST_CUSTOM_SETTER_ONLY, value);
    zassert_equal(ret, -EINVAL);
    zassert_equal(g_setter_only_calls, 0);
}

ZTEST(stow_custom, test_custom_validate_accepts_valid)
{
    /* 10 is in range and even -> passes both regular and custom validation. */
    data_value_t value = { .type = STOW_ITEM_TYPE_INT, .data.int_value = 10 };
    int ret = stow_set(STOW_ID_TEST_CUSTOM_VALIDATED, value);
    zassert_equal(ret, SUCCESS);
    zassert_equal(g_validate_calls, 1);

    data_value_t got;
    ret = stow_get(STOW_ID_TEST_CUSTOM_VALIDATED, &got);
    zassert_equal(ret, SUCCESS);
    zassert_equal(got.data.int_value, 10);
    stow_release(STOW_ID_TEST_CUSTOM_VALIDATED, &got);
}

ZTEST(stow_custom, test_custom_validate_rejects)
{
    /* 9 is in range but odd -> regular validation passes, custom rejects. */
    data_value_t value = { .type = STOW_ITEM_TYPE_INT, .data.int_value = 9 };
    int ret = stow_set(STOW_ID_TEST_CUSTOM_VALIDATED, value);
    zassert_equal(ret, -EINVAL);
    zassert_equal(g_validate_calls, 1);

    /* Value should be unchanged from default (0). */
    data_value_t got;
    ret = stow_get(STOW_ID_TEST_CUSTOM_VALIDATED, &got);
    zassert_equal(ret, SUCCESS);
    zassert_equal(got.data.int_value, 0);
    stow_release(STOW_ID_TEST_CUSTOM_VALIDATED, &got);
}

ZTEST(stow_custom, test_custom_validate_skipped_when_regular_validation_fails)
{
    /* 999 fails the regular range check -> custom validator must not run. */
    data_value_t value = { .type = STOW_ITEM_TYPE_INT, .data.int_value = 999 };
    int ret = stow_set(STOW_ID_TEST_CUSTOM_VALIDATED, value);
    zassert_equal(ret, -EINVAL);
    zassert_equal(g_validate_calls, 0);
}

ZTEST(stow_custom, test_custom_validate_forced_reject)
{
    /* Force the custom validator to reject a value that would otherwise pass. */
    g_validator_rejects = true;
    data_value_t value = { .type = STOW_ITEM_TYPE_INT, .data.int_value = 2 };
    int ret = stow_set(STOW_ID_TEST_CUSTOM_VALIDATED, value);
    zassert_equal(ret, -EINVAL);
    zassert_equal(g_validate_calls, 1);
}
