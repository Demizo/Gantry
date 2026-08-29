/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gantry/error.h>
#include <zephyr/kernel.h>

#include "test_common.h"

#define PAST_FLUSH_DELAY K_MSEC(CONFIG_STOW_STORAGE_FLUSH_DELAY_MS * 2)

static void set_string(enum stow_item_id id, const char* text)
{
    data_value_t value = { .type = STOW_ITEM_TYPE_STRING, .data.string_value = (char*)text };
    zassert_equal(stow_set(id, value), SUCCESS);
}

static void write_during_save(void) { set_string(STOW_ID_TEST_STRING, "landed_mid_save"); }

ZTEST_SUITE(stow_persistence, NULL, NULL, reset_stow, NULL, NULL);

ZTEST(stow_persistence, test_write_is_saved_after_the_flush_delay)
{
    set_string(STOW_ID_TEST_STRING, "pending");
    zassert_equal(g_stow_storage_stub.save_count, 0, "Save must not happen inline with the write");

    k_sleep(PAST_FLUSH_DELAY);
    zassert_equal(stow_storage_stub_item_save_count(STOW_ID_TEST_STRING), 1);
}

ZTEST(stow_persistence, test_repeated_writes_coalesce_into_one_save)
{
    set_string(STOW_ID_TEST_STRING, "one");
    set_string(STOW_ID_TEST_STRING, "two");
    set_string(STOW_ID_TEST_STRING, "three");
    set_string(STOW_ID_TEST_STRING, "four");

    k_sleep(PAST_FLUSH_DELAY);
    zassert_equal(g_stow_storage_stub.save_count, 1);

    // The flush reads whatever is current, so it saves the last write, not the first.
    data_value_t got = { 0 };
    zassert_equal(stow_get(STOW_ID_TEST_STRING, &got), SUCCESS);
    zassert_str_equal(got.data.string_value, "four");
    stow_release(STOW_ID_TEST_STRING, &got);
}

ZTEST(stow_persistence, test_ephemeral_writes_are_never_saved)
{
    data_value_t value = { .type = STOW_ITEM_TYPE_INT, .data.int_value = 7 };
    zassert_equal(stow_set(STOW_ID_TEST_INT, value), SUCCESS);

    k_sleep(PAST_FLUSH_DELAY);
    zassert_equal(g_stow_storage_stub.save_count, 0);
}

ZTEST(stow_persistence, test_write_during_a_save_is_saved_by_the_next_flush)
{
    set_string(STOW_ID_TEST_STRING, "first");
    g_stow_storage_stub.during_save = write_during_save;

    zassert_equal(stow_flush(), SUCCESS);
    zassert_equal(stow_storage_stub_item_save_count(STOW_ID_TEST_STRING), 1);

    // The write that landed mid-save re-marked the item rather than being swallowed by the save
    // that was already in flight.
    zassert_equal(stow_flush(), SUCCESS);
    zassert_equal(stow_storage_stub_item_save_count(STOW_ID_TEST_STRING), 2);
}

ZTEST(stow_persistence, test_flush_saves_every_dirty_item_and_clears_them)
{
    set_string(STOW_ID_TEST_STRING, "persistent");
    set_string(STOW_ID_TEST_TOFU, "tofu");

    zassert_equal(stow_flush(), SUCCESS);
    zassert_equal(g_stow_storage_stub.save_count, 2);
    zassert_equal(stow_storage_stub_item_save_count(STOW_ID_TEST_STRING), 1);
    zassert_equal(stow_storage_stub_item_save_count(STOW_ID_TEST_TOFU), 1);

    zassert_equal(stow_flush(), SUCCESS);
    zassert_equal(g_stow_storage_stub.save_count, 2, "Flush left items dirty");
}

ZTEST(stow_persistence, test_failed_save_is_reported_and_not_retried)
{
    set_string(STOW_ID_TEST_STRING, "doomed");
    g_stow_storage_stub.next_save_result = -EIO;

    zassert_equal(stow_flush(), -EIO);
    zassert_equal(stow_storage_stub_item_save_count(STOW_ID_TEST_STRING), 1);

    // The item was left clean, so a failed save is dropped rather than retried.
    zassert_equal(stow_flush(), SUCCESS);
    zassert_equal(g_stow_storage_stub.save_count, 1);
}

ZTEST(stow_persistence, test_flush_leaves_nothing_for_the_scheduled_flush)
{
    set_string(STOW_ID_TEST_STRING, "flushed_early");
    zassert_equal(stow_flush(), SUCCESS);
    zassert_equal(g_stow_storage_stub.save_count, 1);

    k_sleep(PAST_FLUSH_DELAY);
    zassert_equal(g_stow_storage_stub.save_count, 1);
}
