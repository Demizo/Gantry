/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gantry/error.h>
#include <zephyr/irq_offload.h>
#include <zephyr/kernel.h>

#include "test_common.h"

#define ISR_INT_VALUE 5
#define ISR_STRING_VALUE "from_isr"

static int g_isr_ret;
static int g_isr_get_value;

static void offload_set_ephemeral(const void* arg)
{
    ARG_UNUSED(arg);
    data_value_t value = { .type = STOW_ITEM_TYPE_INT, .data.int_value = ISR_INT_VALUE };
    g_isr_ret = stow_set(STOW_ID_TEST_INT, value);
}

static void offload_set_persistent(const void* arg)
{
    ARG_UNUSED(arg);
    data_value_t value = { .type = STOW_ITEM_TYPE_STRING, .data.string_value = ISR_STRING_VALUE };
    g_isr_ret = stow_set(STOW_ID_TEST_STRING, value);
}

static void offload_set_tofu(const void* arg)
{
    ARG_UNUSED(arg);
    data_value_t value = { .type = STOW_ITEM_TYPE_STRING, .data.string_value = ISR_STRING_VALUE };
    g_isr_ret = stow_set(STOW_ID_TEST_TOFU, value);
}

static void offload_get_ephemeral(const void* arg)
{
    ARG_UNUSED(arg);
    data_value_t got = { 0 };
    g_isr_ret = stow_get(STOW_ID_TEST_INT, &got);
    g_isr_get_value = got.data.int_value;
    stow_release(STOW_ID_TEST_INT, &got);
}

static void isr_subscriber_cb(event_t* event) { ARG_UNUSED(event); }

static void offload_subscribe(const void* arg)
{
    struct stow_subscription* subscription = (struct stow_subscription*)arg;
    g_isr_ret = stow_subscribe(STOW_ID_TEST_INT, subscription);
}

static void offload_unsubscribe(const void* arg)
{
    struct stow_subscription* subscription = (struct stow_subscription*)arg;
    g_isr_ret = stow_unsubscribe(STOW_ID_TEST_INT, subscription);
}

static void assert_string_item(enum stow_item_id id, const char* expected)
{
    data_value_t got = { 0 };
    zassert_equal(stow_get(id, &got), SUCCESS);
    zassert_str_equal(got.data.string_value, expected);
    stow_release(id, &got);
}

ZTEST_SUITE(stow_isr, NULL, NULL, reset_stow, NULL, NULL);

ZTEST(stow_isr, test_set_from_isr_ephemeral)
{
    irq_offload(offload_set_ephemeral, NULL);
    zassert_equal(g_isr_ret, SUCCESS);

    data_value_t got = { 0 };
    zassert_equal(stow_get(STOW_ID_TEST_INT, &got), SUCCESS);
    zassert_equal(got.data.int_value, ISR_INT_VALUE);
}

ZTEST(stow_isr, test_set_from_isr_persistent)
{
    irq_offload(offload_set_persistent, NULL);
    zassert_equal(g_isr_ret, SUCCESS);
    assert_string_item(STOW_ID_TEST_STRING, ISR_STRING_VALUE);
}

ZTEST(stow_isr, test_set_from_isr_tofu)
{
    irq_offload(offload_set_tofu, NULL);
    zassert_equal(g_isr_ret, SUCCESS);
    assert_string_item(STOW_ID_TEST_TOFU, ISR_STRING_VALUE);

    // The ISR write consumed the item's one-time write, just as a thread-context write would.
    data_value_t value = { .type = STOW_ITEM_TYPE_STRING, .data.string_value = "from_thread" };
    zassert_equal(stow_set(STOW_ID_TEST_TOFU, value), -EACCES);
}

ZTEST(stow_isr, test_persistent_set_from_isr_is_flushed)
{
    irq_offload(offload_set_persistent, NULL);
    zassert_equal(g_isr_ret, SUCCESS);

    k_msleep(CONFIG_STOW_STORAGE_FLUSH_DELAY_MS * 2);
    zassert_equal(stow_storage_stub_item_save_count(STOW_ID_TEST_STRING), 1);
}

ZTEST(stow_isr, test_get_from_isr)
{
    data_value_t value = { .type = STOW_ITEM_TYPE_INT, .data.int_value = 21 };
    zassert_equal(stow_set(STOW_ID_TEST_INT, value), SUCCESS);

    irq_offload(offload_get_ephemeral, NULL);
    zassert_equal(g_isr_ret, SUCCESS);
    zassert_equal(g_isr_get_value, 21);
}

ZTEST(stow_isr, test_subscribe_and_unsubscribe_from_isr)
{
    struct stow_subscription sub = {
        .mode = STOW_SUBSCRIPTION_HANDLE,
        .cb = isr_subscriber_cb,
    };

    irq_offload(offload_subscribe, &sub);
    zassert_equal(g_isr_ret, SUCCESS);

    irq_offload(offload_unsubscribe, &sub);
    zassert_equal(g_isr_ret, SUCCESS);
}
