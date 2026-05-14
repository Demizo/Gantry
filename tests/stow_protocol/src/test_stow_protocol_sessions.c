/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "gantry/stow/stow_serde.h"
#include "test_common.h"

#define SESSION_BASE 41

static uint32_t g_open_sessions[CONFIG_STOW_PROTOCOL_MAX_SESSIONS];
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
        (void)stow_protocol_session_closed(g_open_sessions[i]);
    }
    test_sync();
    test_drain_captures();
}

ZTEST_SUITE(stow_protocol_sessions, NULL, NULL, before_each, after_each, NULL);

static void open_tracked(uint32_t session_id, stow_role_t roles)
{
    int ret = stow_protocol_session_open(session_id, roles);
    zassert_equal(ret, 0, "session_open(%u) failed: %d", session_id, ret);
    zassert_true(g_open_session_count < ARRAY_SIZE(g_open_sessions));
    g_open_sessions[g_open_session_count++] = session_id;
}

ZTEST(stow_protocol_sessions, test_max_sessions_then_one_more_returns_enomem)
{
    for (uint32_t i = 0; i < CONFIG_STOW_PROTOCOL_MAX_SESSIONS; i++)
    {
        open_tracked(SESSION_BASE + i, STOW_ROLE_GUEST);
    }
    int ret = stow_protocol_session_open(SESSION_BASE + CONFIG_STOW_PROTOCOL_MAX_SESSIONS, STOW_ROLE_GUEST);
    zassert_equal(ret, -ENOMEM);
}

ZTEST(stow_protocol_sessions, test_close_frees_slot)
{
    for (uint32_t i = 0; i < CONFIG_STOW_PROTOCOL_MAX_SESSIONS; i++)
    {
        open_tracked(SESSION_BASE + i, STOW_ROLE_GUEST);
    }
    // Now the table is full. Close one, sync, then a new id can be opened.
    zassert_equal(stow_protocol_session_closed(SESSION_BASE), 0);
    test_sync();

    int ret = stow_protocol_session_open(0xABCD, STOW_ROLE_GUEST);
    zassert_equal(ret, 0);
    (void)stow_protocol_session_closed(0xABCD);
}

ZTEST(stow_protocol_sessions, test_multi_session_isolated_responses)
{
    open_tracked(SESSION_BASE, STOW_ROLE_GUEST);
    open_tracked(SESSION_BASE + 1, STOW_ROLE_GUEST);

    // Send Version to each, in order
    uint8_t req[8];
    int len = test_build_simple(req, sizeof(req), STOW_MSG_VERSION);
    zassert_equal(test_submit_rx(SESSION_BASE, STOW_ROLE_GUEST, req, len), 0);
    zassert_equal(test_submit_rx(SESSION_BASE + 1, STOW_ROLE_GUEST, req, len), 0);

    // Capture two responses; each must come back with the right session id.
    bool got_a = false, got_b = false;
    for (int i = 0; i < 2; i++)
    {
        struct test_capture cap = test_await_tx(K_SECONDS(1));
        zassert_not_null(cap.buf);
        if (cap.session_id == SESSION_BASE) got_a = true;
        if (cap.session_id == SESSION_BASE + 1) got_b = true;
        net_buf_unref(cap.buf);
    }
    zassert_true(got_a);
    zassert_true(got_b);
}

ZTEST(stow_protocol_sessions, test_close_then_rx_is_unhandled)
{
    open_tracked(SESSION_BASE, STOW_ROLE_GUEST);

    zassert_equal(stow_protocol_session_closed(SESSION_BASE), 0);
    test_sync();
    test_drain_captures();

    // The session is gone, the next RX should go unhandled.
    uint8_t req[8];
    int len = test_build_simple(req, sizeof(req), STOW_MSG_VERSION);
    zassert_equal(test_submit_rx(SESSION_BASE, STOW_ROLE_GUEST, req, len), 0);

    struct test_capture cap = test_await_tx(K_MSEC(100));
    zassert_is_null(cap.buf);

    g_open_session_count = 0;
}

ZTEST(stow_protocol_sessions, test_set_auth_lower_blocks_subsequent_get)
{
    open_tracked(SESSION_BASE, STOW_ROLE_SESSION);

    // With SESSION role we can read TestSessionInt
    uint8_t req[16];
    int len = test_build_with_id(req, sizeof(req), STOW_MSG_GET, STOW_ID_TEST_SESSION_INT);
    zassert_equal(test_submit_rx(SESSION_BASE, STOW_ROLE_SESSION, req, len), 0);

    struct test_capture cap = test_await_tx(K_SECONDS(1));
    zassert_not_null(cap.buf);
    uint32_t cmd;
    zcbor_state_t dec[4];
    zassert_true(test_decode_response_header(cap.buf, &cmd, dec, ARRAY_SIZE(dec)));
    zassert_equal(cmd, STOW_MSG_GET_RESPONSE, "should have received a Get Response");
    net_buf_unref(cap.buf);

    // Drop roles via dedicated API
    zassert_equal(stow_protocol_session_set_roles(SESSION_BASE, STOW_ROLE_GUEST), 0);
    test_sync();
    test_drain_captures();

    // The same RX (now with STOW_ROLE_GUEST in the message) should be denied
    zassert_equal(test_submit_rx(SESSION_BASE, STOW_ROLE_GUEST, req, len), 0);
    cap = test_await_tx(K_SECONDS(1));
    zassert_not_null(cap.buf);
    zassert_true(test_decode_response_header(cap.buf, &cmd, dec, ARRAY_SIZE(dec)));
    zassert_equal(cmd, STOW_MSG_ERROR);
    net_buf_unref(cap.buf);
}
