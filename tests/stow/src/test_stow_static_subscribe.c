/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "test_common.h"

static bool g_static_handle_called = false;
static int g_static_handle_id = -1;

static bool g_static_copy_called = false;
static int g_static_copy_value = -1;

static bool g_static_multi_called = false;
static int g_static_multi_id = -1;

static bool g_dynamic_called = false;

static void static_handle_callback(event_t* event)
{
    struct stow_update_event_payload* payload = (struct stow_update_event_payload*)event->data.buf;
    g_static_handle_called = true;
    g_static_handle_id = (int)payload->metadata->id;
}

static void static_copy_callback(event_t* event)
{
    struct stow_update_event_payload* payload = (struct stow_update_event_payload*)event->data.buf;
    g_static_copy_called = true;
    g_static_copy_value = payload->value_copy.data.int_value;
}

static void static_multi_callback(event_t* event)
{
    struct stow_update_event_payload* payload = (struct stow_update_event_payload*)event->data.buf;
    g_static_multi_called = true;
    g_static_multi_id = (int)payload->metadata->id;
}

static void dynamic_callback(event_t* event)
{
    ARG_UNUSED(event);
    g_dynamic_called = true;
}

STOW_SUBSCRIPTION_DEFINE(
    test_static_handle_sub, STOW_SUBSCRIPTION_HANDLE, static_handle_callback, STOW_ID_TEST_STATIC_SUBSCRIBE_INT);
STOW_SUBSCRIPTION_DEFINE(
    test_static_copy_sub, STOW_SUBSCRIPTION_COPY, static_copy_callback, STOW_ID_TEST_STATIC_SUBSCRIBE_INT);
STOW_SUBSCRIPTION_DEFINE(
    test_static_multi_sub, STOW_SUBSCRIPTION_HANDLE, static_multi_callback, STOW_ID_TEST_STATIC_SUBSCRIBE_INT,
    STOW_ID_TEST_STATIC_SUBSCRIBE_INT2);

static void reset_stow_and_flags(void* fixture)
{
    (void)fixture;
    stow_init();
    g_static_handle_called = false;
    g_static_handle_id = -1;
    g_static_copy_called = false;
    g_static_copy_value = -1;
    g_static_multi_called = false;
    g_static_multi_id = -1;
    g_dynamic_called = false;
}

ZTEST_SUITE(stow_static_subscribe, NULL, NULL, reset_stow_and_flags, NULL, NULL);

ZTEST(stow_static_subscribe, test_static_handle_subscription)
{
    data_value_t val = { .type = STOW_ITEM_TYPE_INT, .data.int_value = 7 };
    stow_set(STOW_ROLE_GUEST, STOW_ID_TEST_STATIC_SUBSCRIBE_INT, val);

    zassert_true(g_static_handle_called);
    zassert_equal(g_static_handle_id, (int)STOW_ID_TEST_STATIC_SUBSCRIBE_INT);
}

ZTEST(stow_static_subscribe, test_static_copy_subscription)
{
    data_value_t val = { .type = STOW_ITEM_TYPE_INT, .data.int_value = 42 };
    stow_set(STOW_ROLE_GUEST, STOW_ID_TEST_STATIC_SUBSCRIBE_INT, val);

    zassert_true(g_static_copy_called);
    zassert_equal(g_static_copy_value, 42);
}

ZTEST(stow_static_subscribe, test_static_multi_id_subscription)
{
    data_value_t val1 = { .type = STOW_ITEM_TYPE_INT, .data.int_value = 1 };
    stow_set(STOW_ROLE_GUEST, STOW_ID_TEST_STATIC_SUBSCRIBE_INT, val1);

    zassert_true(g_static_multi_called);
    zassert_equal(g_static_multi_id, (int)STOW_ID_TEST_STATIC_SUBSCRIBE_INT);

    g_static_multi_called = false;
    g_static_multi_id = -1;

    data_value_t val2 = { .type = STOW_ITEM_TYPE_INT, .data.int_value = 2 };
    stow_set(STOW_ROLE_GUEST, STOW_ID_TEST_STATIC_SUBSCRIBE_INT2, val2);

    zassert_true(g_static_multi_called);
    zassert_equal(g_static_multi_id, (int)STOW_ID_TEST_STATIC_SUBSCRIBE_INT2);
}

ZTEST(stow_static_subscribe, test_static_and_dynamic_coexist)
{
    struct stow_subscription dyn_sub = {
        .mode = STOW_SUBSCRIPTION_HANDLE,
        .cb = dynamic_callback,
    };
    int ret = stow_subscribe(STOW_ROLE_GUEST, STOW_ID_TEST_STATIC_SUBSCRIBE_INT, &dyn_sub);
    zassert_equal(ret, 0);

    data_value_t val = { .type = STOW_ITEM_TYPE_INT, .data.int_value = 13 };
    stow_set(STOW_ROLE_GUEST, STOW_ID_TEST_STATIC_SUBSCRIBE_INT, val);

    zassert_true(g_static_handle_called);
    zassert_true(g_dynamic_called);

    stow_unsubscribe(STOW_ID_TEST_STATIC_SUBSCRIBE_INT, &dyn_sub);
}
