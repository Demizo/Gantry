/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gantry/error.h>

#include "test_common.h"

static bool g_callback_called = false;
static int g_callback_handle_id = -1;
static int g_callback_copy_value = -1;

static void handle_callback(event_t* event)
{
    struct stow_update_event_payload* payload = (struct stow_update_event_payload*)event->data.buf;
    g_callback_called = true;
    g_callback_handle_id = (int)payload->metadata->id;
}

static void copy_callback(event_t* event)
{
    struct stow_update_event_payload* payload = (struct stow_update_event_payload*)event->data.buf;
    g_callback_called = true;
    g_callback_copy_value = payload->value_copy.data.int_value;
}

static void reset_stow_and_flags(void* fixture)
{
    (void)fixture;
    stow_init();
    g_callback_called = false;
    g_callback_handle_id = -1;
    g_callback_copy_value = -1;
}

ZTEST_SUITE(stow_subscribe, NULL, NULL, reset_stow_and_flags, NULL, NULL);

ZTEST(stow_subscribe, test_handle_subscription)
{
    struct stow_subscription sub = {
        .mode = STOW_SUBSCRIPTION_HANDLE,
        .cb = handle_callback,
    };
    int ret = stow_subscribe(STOW_ID_TEST_INT, &sub);
    zassert_equal(ret, 0);

    data_value_t val = { .type = STOW_ITEM_TYPE_INT, .data.int_value = 7 };
    stow_set(STOW_ID_TEST_INT, val);

    zassert_true(g_callback_called);
    zassert_equal(g_callback_handle_id, (int)STOW_ID_TEST_INT);

    ret = stow_unsubscribe(STOW_ID_TEST_INT, &sub);
    zassert_equal(ret, 0);
}

ZTEST(stow_subscribe, test_copy_subscription)
{
    struct stow_subscription sub = {
        .mode = STOW_SUBSCRIPTION_COPY,
        .cb = copy_callback,
    };
    int ret = stow_subscribe(STOW_ID_TEST_INT, &sub);
    zassert_equal(ret, 0);

    data_value_t val = { .type = STOW_ITEM_TYPE_INT, .data.int_value = 42 };
    stow_set(STOW_ID_TEST_INT, val);

    zassert_true(g_callback_called);
    zassert_equal(g_callback_copy_value, 42);

    stow_unsubscribe(STOW_ID_TEST_INT, &sub);
}

ZTEST(stow_subscribe, test_duplicate_subscription)
{
    struct stow_subscription sub = {
        .mode = STOW_SUBSCRIPTION_HANDLE,
        .cb = handle_callback,
    };
    int ret = stow_subscribe(STOW_ID_TEST_INT, &sub);
    zassert_equal(ret, 0);

    ret = stow_subscribe(STOW_ID_TEST_INT, &sub);
    zassert_equal(ret, -EALREADY);

    stow_unsubscribe(STOW_ID_TEST_INT, &sub);
}

ZTEST(stow_subscribe, test_unsubscribe_not_subscribed)
{
    struct stow_subscription sub = {
        .mode = STOW_SUBSCRIPTION_HANDLE,
        .cb = handle_callback,
    };
    int ret = stow_unsubscribe(STOW_ID_TEST_INT, &sub);
    zassert_equal(ret, -ENOENT);
}

ZTEST(stow_subscribe, test_no_callback_after_unsubscribe)
{
    struct stow_subscription sub = {
        .mode = STOW_SUBSCRIPTION_HANDLE,
        .cb = handle_callback,
    };
    stow_subscribe(STOW_ID_TEST_INT, &sub);
    stow_unsubscribe(STOW_ID_TEST_INT, &sub);

    data_value_t val = { .type = STOW_ITEM_TYPE_INT, .data.int_value = 5 };
    stow_set(STOW_ID_TEST_INT, val);

    zassert_false(g_callback_called);
}

static int g_reentrant_get_value;
static int g_reentrant_set_ret;

static void reentrant_callback(event_t* event)
{
    ARG_UNUSED(event);

    data_value_t got = { 0 };
    stow_get(STOW_ID_TEST_INT, &got);
    g_reentrant_get_value = got.data.int_value;
    stow_release(STOW_ID_TEST_INT, &got);

    data_value_t other = { .type = STOW_ITEM_TYPE_FLOAT, .data.float_value = 3.5f };
    g_reentrant_set_ret = stow_set(STOW_ID_TEST_FLOAT, other);
}

ZTEST(stow_subscribe, test_reentrant_callback_does_not_deadlock)
{
    g_reentrant_get_value = -1;
    g_reentrant_set_ret = -1;

    struct stow_subscription sub = {
        .mode = STOW_SUBSCRIPTION_HANDLE,
        .cb = reentrant_callback,
    };
    int ret = stow_subscribe(STOW_ID_TEST_INT, &sub);
    zassert_equal(ret, 0);

    data_value_t val = { .type = STOW_ITEM_TYPE_INT, .data.int_value = 11 };
    ret = stow_set(STOW_ID_TEST_INT, val);
    zassert_equal(ret, SUCCESS);

    zassert_equal(g_reentrant_get_value, 11);
    zassert_equal(g_reentrant_set_ret, SUCCESS);

    data_value_t other_check = { 0 };
    stow_get(STOW_ID_TEST_FLOAT, &other_check);
    zassert_within(other_check.data.float_value, 3.5f, 0.0001f);

    stow_unsubscribe(STOW_ID_TEST_INT, &sub);
}
