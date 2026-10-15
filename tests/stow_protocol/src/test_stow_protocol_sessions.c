/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "gantry/stow/stow_serde.h"
#include "test_common.h"

#define SESSION_BASE 41

static uint32_t g_open_sessions[TEST_FREE_SESSIONS];
static size_t g_open_session_count;

static void before_each(void* fixture)
{
    (void)fixture;
    test_reset();
    g_open_session_count = 0;
}

static void after_each(void* fixture)
{
    (void)fixture;
    for (size_t i = 0; i < g_open_session_count; i++)
    {
        test_endpoint_close(g_open_sessions[i]);
    }
    test_sync();
    test_drain_captures();
}

ZTEST_SUITE(stow_protocol_sessions, NULL, NULL, before_each, after_each, NULL);

static void open_tracked(uint32_t session_id, stow_role_t roles)
{
    int ret = test_endpoint_open(session_id, roles, &test_transport);
    zassert_equal(ret, 0, "session_open(%u) failed: %d", session_id, ret);
    zassert_true(g_open_session_count < ARRAY_SIZE(g_open_sessions));
    g_open_sessions[g_open_session_count++] = session_id;
}

ZTEST(stow_protocol_sessions, test_max_sessions_then_one_more_returns_enomem)
{
    for (uint32_t i = 0; i < TEST_FREE_SESSIONS; i++)
    {
        open_tracked(SESSION_BASE + i, STOW_ROLE_GUEST);
    }
    int ret = test_endpoint_open(SESSION_BASE + TEST_FREE_SESSIONS, STOW_ROLE_GUEST, &test_transport);
    zassert_equal(ret, -ENOMEM);
}

ZTEST(stow_protocol_sessions, test_close_frees_slot)
{
    for (uint32_t i = 0; i < TEST_FREE_SESSIONS; i++)
    {
        open_tracked(SESSION_BASE + i, STOW_ROLE_GUEST);
    }
    // Now the table is full. Close one, sync, then a new id can be opened.
    test_endpoint_close(SESSION_BASE);
    test_sync();

    int ret = test_endpoint_open(0xABCD, STOW_ROLE_GUEST, &test_transport);
    zassert_equal(ret, 0);
    test_endpoint_close(0xABCD);
}

ZTEST(stow_protocol_sessions, test_open_detects_duplicate_behind_free_slot)
{
    open_tracked(SESSION_BASE, STOW_ROLE_GUEST);
    open_tracked(SESSION_BASE + 1, STOW_ROLE_GUEST);

    test_endpoint_close(SESSION_BASE);
    test_sync();

    int ret = test_endpoint_open(SESSION_BASE + 1, STOW_ROLE_GUEST, &test_transport);
    zassert_equal(ret, -EALREADY, "duplicate session opened: %d", ret);
}

ZTEST(stow_protocol_sessions, test_two_sessions_on_one_transport_get_their_own_responses)
{
    open_tracked(SESSION_BASE, STOW_ROLE_GUEST);
    open_tracked(SESSION_BASE + 1, STOW_ROLE_GUEST);

    uint8_t req[8];
    int len = test_build_simple(req, sizeof(req), STOW_MSG_HELLO, 1);
    zassert_equal(test_submit_rx(SESSION_BASE, req, len), 0);
    len = test_build_simple(req, sizeof(req), STOW_MSG_HELLO, 2);
    zassert_equal(test_submit_rx(SESSION_BASE + 1, req, len), 0);

    bool got_a = false;
    bool got_b = false;
    for (int i = 0; i < 2; i++)
    {
        struct test_capture cap = test_await_tx(K_SECONDS(1));
        zassert_not_null(cap.buf);
        uint32_t cmd = 0;
        uint32_t tag = 0;
        zcbor_state_t dec[4];
        zassert_true(test_decode_response_header(cap.buf, &cmd, &tag, dec, ARRAY_SIZE(dec)));
        zassert_equal(cmd, STOW_MSG_HELLO_RESPONSE);
        if (cap.session_id == SESSION_BASE)
        {
            zassert_equal(tag, 1, "session A got session B's response");
            got_a = true;
        }
        if (cap.session_id == SESSION_BASE + 1)
        {
            zassert_equal(tag, 2, "session B got session A's response");
            got_b = true;
        }
        net_buf_unref(cap.buf);
    }
    zassert_true(got_a);
    zassert_true(got_b);
}

ZTEST(stow_protocol_sessions, test_close_then_rx_is_dropped)
{
    open_tracked(SESSION_BASE, STOW_ROLE_GUEST);

    test_endpoint_close(SESSION_BASE);
    test_sync();
    test_drain_captures();

    uint8_t req[8];
    int len = test_build_simple(req, sizeof(req), STOW_MSG_HELLO, TEST_TAG);
    zassert_equal(test_submit_rx(SESSION_BASE, req, len), 0);
    test_sync();

    struct test_capture cap = test_await_tx(K_MSEC(100));
    zassert_is_null(cap.buf);

    g_open_session_count = 0;
}

ZTEST(stow_protocol_sessions, test_set_auth_lower_blocks_subsequent_get)
{
    open_tracked(SESSION_BASE, STOW_ROLE_SESSION);

    // With SESSION role we can read TestSessionInt
    uint8_t req[16];
    int len = test_build_with_id(req, sizeof(req), STOW_MSG_GET, TEST_TAG, STOW_ID_TEST_SESSION_INT);
    zassert_equal(test_submit_rx(SESSION_BASE, req, len), 0);

    struct test_capture cap = test_await_tx(K_SECONDS(1));
    zassert_not_null(cap.buf);
    uint32_t cmd;
    zcbor_state_t dec[4];
    zassert_true(test_decode_response_header(cap.buf, &cmd, NULL, dec, ARRAY_SIZE(dec)));
    zassert_equal(cmd, STOW_MSG_GET_RESPONSE, "should have received a Get Response");
    net_buf_unref(cap.buf);

    // Drop roles via dedicated API
    zassert_equal(stow_protocol_session_set_roles(SESSION_BASE, STOW_ROLE_GUEST), 0);
    test_sync();
    test_drain_captures();

    zassert_equal(test_submit_rx(SESSION_BASE, req, len), 0);
    cap = test_await_tx(K_SECONDS(1));
    zassert_not_null(cap.buf);
    zassert_true(test_decode_response_header(cap.buf, &cmd, NULL, dec, ARRAY_SIZE(dec)));
    zassert_equal(cmd, STOW_MSG_ERROR);
    net_buf_unref(cap.buf);
}
