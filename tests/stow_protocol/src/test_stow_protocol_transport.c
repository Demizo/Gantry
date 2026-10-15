/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "test_common.h"

#define SESSION_A 61
#define SESSION_B 62
#define OTHER_HEADROOM 16

NET_BUF_POOL_DEFINE(other_pool, 4, TEST_BUF_DATA_SIZE, 0, NULL);
NET_BUF_POOL_VAR_DEFINE(variable_pool, 2, 256, 0, NULL);

K_MSGQ_DEFINE(other_capture_msgq, sizeof(struct net_buf*), 4, sizeof(void*));

static int other_send(struct stow_transport* transport, uint32_t session_id, struct net_buf* buf)
{
    ARG_UNUSED(transport);
    ARG_UNUSED(session_id);
    return k_msgq_put(&other_capture_msgq, &buf, K_NO_WAIT) == 0 ? 0 : -ENOBUFS;
}

static struct stow_transport other_transport = {
    .send = other_send,
    .pool = &other_pool,
    .headroom = OTHER_HEADROOM,
    .tailroom = 0,
};

static void before_each(void* fixture)
{
    (void)fixture;
    test_reset();
}

static void after_each(void* fixture)
{
    (void)fixture;
    test_refuse_sends(false);
    test_endpoint_close(SESSION_A);
    test_endpoint_close(SESSION_B);
    test_sync();
    test_drain_captures();

    struct net_buf* buf = NULL;
    while (k_msgq_get(&other_capture_msgq, &buf, K_NO_WAIT) == 0)
    {
        net_buf_unref(buf);
    }
}

ZTEST_SUITE(stow_protocol_transport, NULL, NULL, before_each, after_each, NULL);

static uint32_t response_code(struct net_buf* buf)
{
    uint32_t cmd = UINT32_MAX;
    zcbor_state_t dec[4];
    zassert_true(test_decode_response_header(buf, &cmd, dec, ARRAY_SIZE(dec)));
    return cmd;
}

ZTEST(stow_protocol_transport, test_refused_response_is_dropped_and_next_request_answered)
{
    test_open_session(SESSION_A, STOW_ROLE_GUEST);
    test_refuse_sends(true);

    uint8_t req[8];
    int len = test_build_simple(req, sizeof(req), STOW_MSG_HELLO);
    zassert_equal(test_submit_rx(SESSION_A, req, len), 0);
    test_sync();
    zassert_is_null(test_await_tx(K_MSEC(50)).buf, "refused response was delivered");

    test_refuse_sends(false);
    test_sync();
    zassert_is_null(test_await_tx(K_MSEC(50)).buf, "refused response was sent again");

    zassert_equal(test_submit_rx(SESSION_A, req, len), 0);
    struct test_capture cap = test_await_tx(K_SECONDS(1));
    zassert_not_null(cap.buf, "next request was not answered");
    zassert_equal(cap.session_id, SESSION_A);
    zassert_equal(response_code(cap.buf), STOW_MSG_HELLO_RESPONSE);
    net_buf_unref(cap.buf);
}

ZTEST(stow_protocol_transport, test_session_uses_its_own_transport)
{
    test_open_session(SESSION_A, STOW_ROLE_GUEST);
    zassert_equal(test_endpoint_open(SESSION_B, STOW_ROLE_GUEST, &other_transport), 0);

    uint8_t req[8];
    int len = test_build_simple(req, sizeof(req), STOW_MSG_HELLO);
    zassert_equal(test_submit_rx(SESSION_B, req, len), 0);

    struct net_buf* buf = NULL;
    zassert_equal(k_msgq_get(&other_capture_msgq, &buf, K_SECONDS(1)), 0, "no response on the session's transport");
    zassert_equal(net_buf_headroom(buf), OTHER_HEADROOM);
    zassert_equal(response_code(buf), STOW_MSG_HELLO_RESPONSE);
    net_buf_unref(buf);

    zassert_is_null(test_await_tx(K_MSEC(50)).buf, "response leaked onto another transport");
}

ZTEST(stow_protocol_transport, test_open_without_transport_is_rejected)
{
    zassert_equal(test_endpoint_open(SESSION_A, STOW_ROLE_GUEST, NULL), -EINVAL);
}

ZTEST(stow_protocol_transport, test_open_without_send_or_pool_is_rejected)
{
    struct stow_transport incomplete = test_transport;
    incomplete.send = NULL;
    zassert_equal(test_endpoint_open(SESSION_A, STOW_ROLE_GUEST, &incomplete), -EINVAL);

    incomplete = test_transport;
    incomplete.pool = NULL;
    zassert_equal(test_endpoint_open(SESSION_A, STOW_ROLE_GUEST, &incomplete), -EINVAL);
}

ZTEST(stow_protocol_transport, test_open_with_variable_size_pool_is_rejected)
{
    struct stow_transport variable = test_transport;
    variable.pool = &variable_pool;
    zassert_equal(test_endpoint_open(SESSION_A, STOW_ROLE_GUEST, &variable), -EINVAL);
}

ZTEST(stow_protocol_transport, test_endpoint_drives_the_session_lifecycle)
{
    const struct stow_endpoint* endpoint = &stow_protocol_endpoint;
    zassert_equal(endpoint->session_opened(endpoint, SESSION_A, STOW_ROLE_GUEST, &test_transport), 0);

    struct net_buf* rx = test_alloc_rx();
    zassert_not_null(rx);
    int len = test_build_simple(net_buf_tail(rx), net_buf_tailroom(rx), STOW_MSG_HELLO);
    net_buf_add(rx, len);
    endpoint->receive(endpoint, SESSION_A, rx);
    net_buf_unref(rx);

    struct test_capture cap = test_await_tx(K_SECONDS(1));
    zassert_not_null(cap.buf);
    zassert_equal(response_code(cap.buf), STOW_MSG_HELLO_RESPONSE);
    net_buf_unref(cap.buf);

    endpoint->session_closed(endpoint, SESSION_A);
    test_sync();

    rx = test_alloc_rx();
    zassert_not_null(rx);
    net_buf_add(rx, test_build_simple(net_buf_tail(rx), net_buf_tailroom(rx), STOW_MSG_HELLO));
    endpoint->receive(endpoint, SESSION_A, rx);
    net_buf_unref(rx);
    test_sync();
    zassert_is_null(test_await_tx(K_MSEC(50)).buf, "closed session answered");
}
