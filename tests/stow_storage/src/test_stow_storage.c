/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gantry/stow/stow.h>
#include <generated_stow_items.h>
#include <zephyr/ztest.h>

#include "generated_struct_Level.h"
#include "generated_struct_Setting.h"

static void* setup(void)
{
    /* At boot the Stow loads before native_sim's flash is ready */
    stow_init();
    return NULL;
}

ZTEST_SUITE(stow_storage, NULL, setup, NULL, NULL, NULL);

ZTEST(stow_storage, test_a_persisted_nested_struct_loads_again)
{
    Level_t level = { .value = 42 };
    Setting_t setting = { .level = &level, .flag = 1 };
    data_value_t value = { .type = STOW_ITEM_TYPE_STRUCT, .data.raw_value = &setting };
    zassert_ok(stow_set(STOW_ID_TEST_NESTED_SETTING, value));
    zassert_ok(stow_flush());

    stow_init();

    data_value_t loaded = { 0 };
    zassert_ok(stow_get(STOW_ID_TEST_NESTED_SETTING, &loaded));
    const Setting_t* got = loaded.data.raw_value;
    zassert_equal(got->level->value, 42, "the stored value did not load");
    zassert_equal(got->flag, 1);
    stow_release(STOW_ID_TEST_NESTED_SETTING, &loaded);
}
