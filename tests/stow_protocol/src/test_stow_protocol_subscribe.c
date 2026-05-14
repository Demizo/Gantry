/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "gantry/stow/stow_serde.h"
#include "test_common.h"

#define SESSION_A 31
#define SESSION_B 32

static void before_each(void* fixture)
{
    (void)fixture;
    test_reset();
}

static void after_each(void* fixture)
{
    (void)fixture;
    (void)stow_protocol_session_closed(SESSION_A);
    (void)stow_protocol_session_closed(SESSION_B);
    test_sync();
    test_drain_captures();
}

ZTEST_SUITE(stow_protocol_subscribe, NULL, NULL, before_each, after_each, NULL);

/**
 * @brief Submit a subscribe/unsubscribe request and verify it returns OK
 */
static void subscribe_ok(uint32_t session_id, stow_role_t roles, uint32_t cmd, uint32_t item_id)
{
    uint8_t req[16];
    int len = test_build_with_id(req, sizeof(req), cmd, item_id);
    zassert_equal(test_submit_rx(session_id, roles, req, len), 0);

    struct test_capture cap = test_await_tx(K_SECONDS(1));
    zassert_not_null(cap.buf);
    zassert_equal(cap.session_id, session_id);

    uint32_t resp_cmd;
    zcbor_state_t dec[4];
    zassert_true(test_decode_response_header(cap.buf, &resp_cmd, dec, ARRAY_SIZE(dec)));
    zassert_equal(resp_cmd, STOW_MSG_OK, "expected OK (got %u)", resp_cmd);
    net_buf_unref(cap.buf);
}

ZTEST(stow_protocol_subscribe, test_subscribe_then_set_delivers_update)
{
    zassert_equal(stow_protocol_session_open(SESSION_A, STOW_ROLE_GUEST), 0);

    subscribe_ok(SESSION_A, STOW_ROLE_GUEST, STOW_MSG_SUBSCRIBE, STOW_ID_TEST_INT);

    // Change the value from elsewhere, protocol should send out an Update
    data_value_t v = { .type = STOW_ITEM_TYPE_INT, .data.int_value = 13 };
    zassert_equal(stow_set(STOW_ROLE_INTERNAL, STOW_ID_TEST_INT, v), 0);

    struct test_capture cap = test_await_tx(K_SECONDS(1));
    zassert_not_null(cap.buf, "expected Update message");
    zassert_equal(cap.session_id, SESSION_A);

    uint32_t cmd;
    zcbor_state_t dec[4];
    zassert_true(test_decode_response_header(cap.buf, &cmd, dec, ARRAY_SIZE(dec)));
    zassert_equal(cmd, STOW_MSG_UPDATE, "expected Update (got %u)", cmd);
    uint32_t id;
    zassert_true(zcbor_uint32_decode(dec, &id));
    zassert_equal(id, STOW_ID_TEST_INT);
    int32_t value;
    zassert_true(zcbor_int32_decode(dec, &value));
    zassert_equal(value, 13);
    net_buf_unref(cap.buf);
}

ZTEST(stow_protocol_subscribe, test_resubscribe_is_idempotent)
{
    zassert_equal(stow_protocol_session_open(SESSION_A, STOW_ROLE_GUEST), 0);
    subscribe_ok(SESSION_A, STOW_ROLE_GUEST, STOW_MSG_SUBSCRIBE, STOW_ID_TEST_INT);
    // Re-subscribing should also return OK
    subscribe_ok(SESSION_A, STOW_ROLE_GUEST, STOW_MSG_SUBSCRIBE, STOW_ID_TEST_INT);

    // Only one update should fire on a single set
    data_value_t v = { .type = STOW_ITEM_TYPE_INT, .data.int_value = 4 };
    zassert_equal(stow_set(STOW_ROLE_INTERNAL, STOW_ID_TEST_INT, v), 0);

    struct test_capture cap = test_await_tx(K_SECONDS(1));
    zassert_not_null(cap.buf);
    net_buf_unref(cap.buf);

    cap = test_await_tx(K_MSEC(50));
    zassert_is_null(cap.buf, "did not expect a second update");
}

ZTEST(stow_protocol_subscribe, test_unsubscribe_stops_updates)
{
    zassert_equal(stow_protocol_session_open(SESSION_A, STOW_ROLE_GUEST), 0);
    subscribe_ok(SESSION_A, STOW_ROLE_GUEST, STOW_MSG_SUBSCRIBE, STOW_ID_TEST_INT);
    subscribe_ok(SESSION_A, STOW_ROLE_GUEST, STOW_MSG_UNSUBSCRIBE, STOW_ID_TEST_INT);

    data_value_t v = { .type = STOW_ITEM_TYPE_INT, .data.int_value = 8 };
    zassert_equal(stow_set(STOW_ROLE_INTERNAL, STOW_ID_TEST_INT, v), 0);

    struct test_capture cap = test_await_tx(K_MSEC(100));
    zassert_is_null(cap.buf, "expected no update after unsubscribe");
}

ZTEST(stow_protocol_subscribe, test_unsubscribe_without_subscribe_returns_ok)
{
    zassert_equal(stow_protocol_session_open(SESSION_A, STOW_ROLE_GUEST), 0);
    subscribe_ok(SESSION_A, STOW_ROLE_GUEST, STOW_MSG_UNSUBSCRIBE, STOW_ID_TEST_INT);
}

ZTEST(stow_protocol_subscribe, test_subscribe_permission_denied)
{
    zassert_equal(stow_protocol_session_open(SESSION_A, STOW_ROLE_GUEST), 0);

    uint8_t req[16];
    int len = test_build_with_id(req, sizeof(req), STOW_MSG_SUBSCRIBE, STOW_ID_TEST_SESSION_INT);
    zassert_equal(test_submit_rx(SESSION_A, STOW_ROLE_GUEST, req, len), 0);

    struct test_capture cap = test_await_tx(K_SECONDS(1));
    zassert_not_null(cap.buf);

    uint32_t cmd;
    zcbor_state_t dec[4];
    zassert_true(test_decode_response_header(cap.buf, &cmd, dec, ARRAY_SIZE(dec)));
    zassert_equal(cmd, STOW_MSG_ERROR);
    net_buf_unref(cap.buf);
}

ZTEST(stow_protocol_subscribe, test_multi_session_subscribe_fans_out_to_both)
{
    zassert_equal(stow_protocol_session_open(SESSION_A, STOW_ROLE_GUEST), 0);
    zassert_equal(stow_protocol_session_open(SESSION_B, STOW_ROLE_GUEST), 0);
    subscribe_ok(SESSION_A, STOW_ROLE_GUEST, STOW_MSG_SUBSCRIBE, STOW_ID_TEST_INT);
    subscribe_ok(SESSION_B, STOW_ROLE_GUEST, STOW_MSG_SUBSCRIBE, STOW_ID_TEST_INT);

    data_value_t v = { .type = STOW_ITEM_TYPE_INT, .data.int_value = 99 };
    zassert_equal(stow_set(STOW_ROLE_INTERNAL, STOW_ID_TEST_INT, v), 0);

    // Expect updates for both sessions, in either order.
    bool got_a = false, got_b = false;
    for (int i = 0; i < 2; i++)
    {
        struct test_capture cap = test_await_tx(K_SECONDS(1));
        zassert_not_null(cap.buf, "missing update %d", i);

        uint32_t cmd;
        zcbor_state_t dec[4];
        zassert_true(test_decode_response_header(cap.buf, &cmd, dec, ARRAY_SIZE(dec)));
        zassert_equal(cmd, STOW_MSG_UPDATE);

        if (cap.session_id == SESSION_A) got_a = true;
        if (cap.session_id == SESSION_B) got_b = true;
        net_buf_unref(cap.buf);
    }
    zassert_true(got_a);
    zassert_true(got_b);
}

ZTEST(stow_protocol_subscribe, test_session_close_removes_subscription)
{
    zassert_equal(stow_protocol_session_open(SESSION_A, STOW_ROLE_GUEST), 0);
    zassert_equal(stow_protocol_session_open(SESSION_B, STOW_ROLE_GUEST), 0);
    subscribe_ok(SESSION_A, STOW_ROLE_GUEST, STOW_MSG_SUBSCRIBE, STOW_ID_TEST_INT);
    subscribe_ok(SESSION_B, STOW_ROLE_GUEST, STOW_MSG_SUBSCRIBE, STOW_ID_TEST_INT);

    // Close A
    zassert_equal(stow_protocol_session_closed(SESSION_A), 0);
    test_sync();
    test_drain_captures();

    data_value_t v = { .type = STOW_ITEM_TYPE_INT, .data.int_value = 1 };
    zassert_equal(stow_set(STOW_ROLE_INTERNAL, STOW_ID_TEST_INT, v), 0);

    // Only B should still receive an update
    struct test_capture cap = test_await_tx(K_SECONDS(1));
    zassert_not_null(cap.buf);
    zassert_equal(cap.session_id, SESSION_B);
    net_buf_unref(cap.buf);

    cap = test_await_tx(K_MSEC(50));
    zassert_is_null(cap.buf, "did not expect another update");
}

ZTEST(stow_protocol_subscribe, test_auth_drop_auto_revokes_subscription)
{
    // Start with SESSION role so we can subscribe to TestSessionInt
    zassert_equal(stow_protocol_session_open(SESSION_A, STOW_ROLE_SESSION), 0);
    subscribe_ok(SESSION_A, STOW_ROLE_SESSION, STOW_MSG_SUBSCRIBE, STOW_ID_TEST_SESSION_INT);

    // Switch role to GUEST
    zassert_equal(stow_protocol_session_set_roles(SESSION_A, STOW_ROLE_GUEST), 0);
    test_sync();
    test_drain_captures();

    // Modify the item internally
    data_value_t v = { .type = STOW_ITEM_TYPE_INT, .data.int_value = 50 };
    zassert_equal(stow_set(STOW_ROLE_INTERNAL, STOW_ID_TEST_SESSION_INT, v), 0);

    // No update should arrive
    struct test_capture cap = test_await_tx(K_MSEC(200));
    zassert_is_null(cap.buf, "role-revoked subscription should not receive updates");
}

ZTEST(stow_protocol_subscribe, test_auth_raise_does_not_resubscribe)
{
    zassert_equal(stow_protocol_session_open(SESSION_A, STOW_ROLE_SESSION), 0);
    subscribe_ok(SESSION_A, STOW_ROLE_SESSION, STOW_MSG_SUBSCRIBE, STOW_ID_TEST_SESSION_INT);

    // Drop then raise back, subscription should remain revoked
    zassert_equal(stow_protocol_session_set_roles(SESSION_A, STOW_ROLE_GUEST), 0);
    zassert_equal(stow_protocol_session_set_roles(SESSION_A, STOW_ROLE_SESSION), 0);
    test_sync();
    test_drain_captures();

    data_value_t v = { .type = STOW_ITEM_TYPE_INT, .data.int_value = 77 };
    zassert_equal(stow_set(STOW_ROLE_INTERNAL, STOW_ID_TEST_SESSION_INT, v), 0);

    struct test_capture cap = test_await_tx(K_MSEC(200));
    zassert_is_null(cap.buf, "role switch should not re-arm subscription");
}
