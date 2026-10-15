/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gantry/stow/stow_wire.h>

#include "test_common.h"

static struct test_link link;
static struct stow_client_peer* peer;

static void before_each(void* fixture)
{
    ARG_UNUSED(fixture);
    test_reset();
    peer = test_connect(&link, &test_peer_client, STOW_ROLE_GUEST);
}

static void after_each(void* fixture)
{
    ARG_UNUSED(fixture);
    test_disconnect(&link, &peer);
}

ZTEST_SUITE(stow_client_faults, NULL, NULL, before_each, after_each, NULL);

static const uint16_t counter_id = PEER_ID_COUNTER;
static const uint16_t mode_id = PEER_ID_MODE;

static void set_counter(int value)
{
    zassert_equal(stow_set(STOW_ID_COUNTER, (data_value_t){ .type = STOW_ITEM_TYPE_INT, .data.int_value = value }), 0);
}

static int get_counter(void)
{
    struct stow_client_value value = { 0 };
    zassert_equal(stow_client_get(peer, &counter_id, 1, &value), 0);
    int counter = value.value.data.int_value;
    stow_client_release(&test_peer_client, &value);
    return counter;
}

ZTEST(stow_client_faults, test_a_timeout_fails_only_its_request)
{
    struct test_async dropped;
    struct test_async answered;
    test_async_init(&dropped);
    test_async_init(&answered);

    atomic_set(&link.drop_requests, 1);
    zassert_equal(stow_client_get_async(peer, &counter_id, 1, test_capture_cb, &dropped), 0);
    zassert_equal(stow_client_get_async(peer, &mode_id, 1, test_capture_cb, &answered), 0);

    zassert_equal(k_sem_take(&answered.done, K_MSEC(100)), 0);
    zassert_equal(answered.err, 0);
    test_async_release(&answered);
    zassert_equal(k_sem_take(&dropped.done, TEST_WAIT), 0);
    zassert_equal(dropped.err, -ETIMEDOUT);
}

ZTEST(stow_client_faults, test_a_duplicate_response_never_completes_a_live_request)
{
    set_counter(7);
    zassert_equal(stow_set(STOW_ID_MODE, (data_value_t){ .type = STOW_ITEM_TYPE_ENUM, .data.int_value = Mode_ON }), 0);
    atomic_set(&link.duplicate_responses, 1);
    zassert_equal(get_counter(), 7);

    struct test_async live;
    test_async_init(&live);
    atomic_set(&link.hold_responses, 1);
    zassert_equal(stow_client_get_async(peer, &mode_id, 1, test_capture_cb, &live), 0);
    test_await_held(2);
    test_release_held();

    zassert_equal(k_sem_take(&live.done, TEST_WAIT), 0);
    zassert_equal(live.err, 0);
    zassert_equal(test_async_values(&live)[0].id, PEER_ID_MODE, "answered with the duplicated Counter response");
    zassert_equal(test_async_values(&live)[0].value.data.int_value, peer_Mode_ON);
    test_async_release(&live);
    zassert_equal(get_counter(), 7);
}

ZTEST(stow_client_faults, test_a_refused_send_fails_at_once)
{
    atomic_set(&link.refuse_requests, 1);
    struct stow_client_value value;
    int64_t start = k_uptime_get();
    zassert_equal(stow_client_get(peer, &counter_id, 1, &value), -EBUSY);
    zassert_true(k_uptime_get() - start < CONFIG_STOW_CLIENT_TIMEOUT_MS / 2, "waited for a timeout");

    set_counter(4);
    zassert_equal(get_counter(), 4);

    struct test_link refused;
    test_link_init(&refused, &test_peer_client);
    atomic_set(&refused.refuse_requests, 1);
    zassert_equal(test_open_client(&refused), 0);
    struct test_hook_event event = test_next_hook(TEST_WAIT);
    zassert_equal(event.hook, TEST_HOOK_CONNECTED);
    zassert_equal(event.err, -EBUSY, "a refused Hello did not fail verification");
    struct stow_client_peer* failed = event.peer;
    PASS_OWNERSHIP(failed);
    test_close_client(&refused);
    test_settle();
    stow_client_peer_unref(&failed);
}

ZTEST(stow_client_faults, test_closing_fails_every_pending_request_before_the_hook)
{
    struct test_async pending[3];
    atomic_set(&link.hold_responses, ARRAY_SIZE(pending));
    for (size_t i = 0; i < ARRAY_SIZE(pending); i++)
    {
        test_async_init(&pending[i]);
        zassert_equal(stow_client_get_async(peer, &counter_id, 1, test_capture_cb, &pending[i]), 0);
    }
    test_await_held(ARRAY_SIZE(pending));

    test_close_client(&link);
    struct test_hook_event event = test_next_hook(TEST_WAIT);
    zassert_equal(event.hook, TEST_HOOK_DISCONNECTED);
    for (size_t i = 0; i < ARRAY_SIZE(pending); i++)
    {
        zassert_equal(k_sem_take(&pending[i].done, K_NO_WAIT), 0, "request %zu still pending at the hook", i);
        zassert_equal(pending[i].err, -ENOTCONN);
    }
}

ZTEST(stow_client_faults, test_a_late_response_never_completes_a_newer_request)
{
    struct test_async late;
    test_async_init(&late);
    atomic_set(&link.hold_responses, 1);
    zassert_equal(stow_client_get_async(peer, &counter_id, 1, test_capture_cb, &late), 0);
    zassert_equal(k_sem_take(&late.done, TEST_WAIT), 0);
    zassert_equal(late.err, -ETIMEDOUT);

    for (uint32_t i = 0; i < STOW_TAG_MAX; i++)
    {
        (void)get_counter();
    }

    struct test_async newer;
    test_async_init(&newer);
    atomic_set(&link.hold_responses, 1);
    zassert_equal(stow_client_get_async(peer, &mode_id, 1, test_capture_cb, &newer), 0);
    test_await_held(2);
    test_release_held();

    zassert_equal(k_sem_take(&newer.done, TEST_WAIT), 0);
    zassert_equal(newer.err, 0);
    zassert_equal(test_async_values(&newer)[0].id, PEER_ID_MODE, "completed by the late response");
    test_async_release(&newer);
}

static void fill_table(struct test_async* pending, size_t count)
{
    atomic_set(&link.hold_responses, count);
    for (size_t i = 0; i < count; i++)
    {
        test_async_init(&pending[i]);
        zassert_equal(stow_client_get_async(peer, &counter_id, 1, test_capture_cb, &pending[i]), 0);
    }
}

ZTEST(stow_client_faults, test_a_full_table_makes_async_calls_try_again)
{
    struct test_async pending[CONFIG_STOW_CLIENT_MAX_PENDING];
    fill_table(pending, ARRAY_SIZE(pending));

    struct test_async extra;
    test_async_init(&extra);
    zassert_equal(stow_client_get_async(peer, &counter_id, 1, test_capture_cb, &extra), -EAGAIN);
    for (size_t i = 0; i < ARRAY_SIZE(pending); i++)
    {
        zassert_equal(k_sem_take(&pending[i].done, TEST_WAIT), 0);
        zassert_equal(pending[i].err, -ETIMEDOUT);
    }
}

ZTEST(stow_client_faults, test_a_full_table_makes_sync_calls_wait)
{
    struct test_async pending[CONFIG_STOW_CLIENT_MAX_PENDING];
    fill_table(pending, ARRAY_SIZE(pending));

    set_counter(6);
    zassert_equal(get_counter(), 6, "sync call did not wait for a free slot");
    for (size_t i = 0; i < ARRAY_SIZE(pending); i++)
    {
        zassert_equal(k_sem_take(&pending[i].done, K_NO_WAIT), 0);
        zassert_equal(pending[i].err, -ETIMEDOUT);
    }
}

ZTEST(stow_client_faults, test_a_message_queued_before_a_reopen_is_dropped)
{
    struct test_async pending;
    test_async_init(&pending);
    atomic_set(&link.hold_responses, 1);
    zassert_equal(stow_client_get_async(peer, &counter_id, 1, test_capture_cb, &pending), 0);
    test_await_held(1);

    const uint8_t stale_hello[] = { 0x84, STOW_MSG_HELLO_RESPONSE, 0x00, 0x18, 0x63, 0xA0 };
    struct net_buf* stale = net_buf_alloc(link.to_client.pool, K_NO_WAIT);
    zassert_not_null(stale);
    net_buf_add_mem(stale, stale_hello, sizeof(stale_hello));
    test_peer_client.endpoint.receive(&test_peer_client.endpoint, link.session_id, stale);
    net_buf_unref(stale);

    test_block_disconnected();
    test_close_client(&link);
    test_await_blocked_disconnected();
    zassert_equal(test_open_client(&link), 0);
    test_unblock_disconnected();

    zassert_equal(test_next_hook(TEST_WAIT).hook, TEST_HOOK_DISCONNECTED);
    struct test_hook_event event = test_next_hook(TEST_WAIT);
    zassert_equal(event.hook, TEST_HOOK_CONNECTED);
    zassert_equal(event.err, 0, "verified with the earlier session's message (%d)", event.err);
    struct stow_client_peer* reopened = event.peer;
    stow_client_peer_unref(&reopened);
    zassert_equal(pending.err, -ENOTCONN);
}

ZTEST(stow_client_faults, test_a_session_can_reopen_before_its_close_is_handled)
{
    test_close_client(&link);
    zassert_equal(test_open_client(&link), 0, "the closing session blocked the reopen");

    zassert_equal(test_next_hook(TEST_WAIT).hook, TEST_HOOK_DISCONNECTED);
    struct test_hook_event event = test_next_hook(TEST_WAIT);
    zassert_equal(event.hook, TEST_HOOK_CONNECTED);
    zassert_equal(event.err, 0);
    struct stow_client_peer* reopened = event.peer;
    stow_client_peer_unref(&reopened);
}

ZTEST(stow_client_faults, test_a_late_response_after_a_reopen_never_completes_a_request)
{
    struct test_async old_get;
    test_async_init(&old_get);
    atomic_set(&link.hold_responses, 1);
    zassert_equal(stow_client_get_async(peer, &counter_id, 1, test_capture_cb, &old_get), 0);
    test_await_held(1);

    test_close_client(&link);
    zassert_equal(k_sem_take(&old_get.done, TEST_WAIT), 0);
    zassert_equal(old_get.err, -ENOTCONN);
    zassert_equal(test_next_hook(TEST_WAIT).hook, TEST_HOOK_DISCONNECTED);

    zassert_equal(test_open_client(&link), 0);
    struct test_hook_event event = test_next_hook(TEST_WAIT);
    zassert_equal(event.hook, TEST_HOOK_CONNECTED);
    zassert_equal(event.err, 0);
    struct stow_client_peer* reopened = event.peer;
    PASS_OWNERSHIP(reopened);

    struct test_async new_get;
    test_async_init(&new_get);
    atomic_set(&link.drop_requests, 1);
    zassert_equal(stow_client_get_async(reopened, &mode_id, 1, test_capture_cb, &new_get), 0);
    test_settle();
    test_release_held();

    zassert_equal(k_sem_take(&new_get.done, TEST_WAIT), 0);
    zassert_equal(new_get.err, -ETIMEDOUT, "the late response completed a new request (%d)", new_get.err);

    stow_client_peer_unref(&peer);
    peer = reopened;
}

ZTEST(stow_client_faults, test_a_peer_that_cannot_be_verified_is_reported)
{
    struct test_link silent;
    test_link_init(&silent, &test_peer_client);
    atomic_set(&silent.drop_responses, 1);
    zassert_equal(
        stow_protocol_endpoint.session_opened(
            &stow_protocol_endpoint, silent.session_id, STOW_ROLE_GUEST, &silent.to_client),
        0);
    zassert_equal(test_open_client(&silent), 0);
    zassert_equal(test_open_client(&silent), -EALREADY);

    struct test_hook_event event = test_next_hook(TEST_WAIT);
    zassert_equal(event.hook, TEST_HOOK_CONNECTED);
    zassert_equal(event.err, -ETIMEDOUT);
    struct stow_client_peer* unverified = event.peer;

    struct stow_client_value value;
    zassert_equal(stow_client_get(unverified, &counter_id, 1, &value), -ENOTCONN);
    test_close_client(&silent);
    zassert_equal(test_next_hook(K_MSEC(50)).hook, TEST_HOOK_NONE, "a failed peer reported a disconnect");
    test_disconnect(&silent, &unverified);
    zassert_is_null(unverified);
}
