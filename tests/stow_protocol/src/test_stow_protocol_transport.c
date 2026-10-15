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

static struct net_buf* hoarded[TEST_TX_BUF_COUNT];
static size_t hoarded_count;

static void hoard_tx_buffers(void)
{
    while (hoarded_count < ARRAY_SIZE(hoarded))
    {
        struct net_buf* buf = net_buf_alloc(test_transport.pool, K_NO_WAIT);
        if (buf == NULL)
        {
            break;
        }
        hoarded[hoarded_count++] = buf;
    }
}

static void release_tx_buffers(void)
{
    while (hoarded_count > 0)
    {
        net_buf_unref(hoarded[--hoarded_count]);
    }
}

static void after_each(void* fixture)
{
    (void)fixture;
    release_tx_buffers();
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
    zassert_true(test_decode_response_header(buf, &cmd, NULL, dec, ARRAY_SIZE(dec)));
    return cmd;
}

ZTEST(stow_protocol_transport, test_refused_response_is_dropped_and_next_request_answered)
{
    test_open_session(SESSION_A, STOW_ROLE_GUEST);
    test_refuse_sends(true);

    uint8_t req[8];
    int len = test_build_simple(req, sizeof(req), STOW_MSG_HELLO, TEST_TAG);
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

ZTEST(stow_protocol_transport, test_close_is_not_lost_when_queue_is_full)
{
    test_open_session(SESSION_A, STOW_ROLE_GUEST);
    test_open_session(SESSION_B, STOW_ROLE_GUEST);

    uint8_t req[8];
    int len = test_build_simple(req, sizeof(req), STOW_MSG_HELLO, 3);
    test_block_sends(true);
    zassert_equal(test_submit_rx(SESSION_B, req, len), 0);
    test_await_blocked_send();
    for (int i = 0; i < CONFIG_STOW_PROTOCOL_QUEUE_SIZE; i++)
    {
        zassert_equal(test_submit_rx(SESSION_B, req, len), 0);
    }
    test_endpoint_close(SESSION_A);
    test_block_sends(false);
    test_sync();
    test_drain_captures();

    zassert_equal(test_endpoint_open(SESSION_A, STOW_ROLE_GUEST, &test_transport), 0);
}

ZTEST(stow_protocol_transport, test_closing_session_gets_no_output)
{
    test_open_session(SESSION_A, STOW_ROLE_GUEST);
    test_open_session(SESSION_B, STOW_ROLE_GUEST);

    uint8_t req[16];
    int len = test_build_with_id(req, sizeof(req), STOW_MSG_SUBSCRIBE, 4, STOW_ID_TEST_INT);
    zassert_equal(test_submit_rx(SESSION_A, req, len), 0);
    struct test_capture cap = test_await_tx(K_SECONDS(1));
    zassert_not_null(cap.buf);
    net_buf_unref(cap.buf);

    test_block_sends(true);
    len = test_build_simple(req, sizeof(req), STOW_MSG_HELLO, 5);
    zassert_equal(test_submit_rx(SESSION_B, req, len), 0);
    test_await_blocked_send();
    zassert_equal(test_submit_rx(SESSION_A, req, len), 0);
    data_value_t value = { .type = STOW_ITEM_TYPE_INT, .data.int_value = 2 };
    zassert_equal(stow_set(STOW_ID_TEST_INT, value), 0);
    test_endpoint_close(SESSION_A);
    test_block_sends(false);
    test_sync();

    cap = test_await_tx(K_SECONDS(1));
    zassert_not_null(cap.buf);
    zassert_equal(cap.session_id, SESSION_B);
    net_buf_unref(cap.buf);
    zassert_is_null(test_await_tx(K_MSEC(50)).buf, "a closing session received output");
}

ZTEST(stow_protocol_transport, test_session_uses_its_own_transport)
{
    test_open_session(SESSION_A, STOW_ROLE_GUEST);
    zassert_equal(test_endpoint_open(SESSION_B, STOW_ROLE_GUEST, &other_transport), 0);

    uint8_t req[8];
    int len = test_build_simple(req, sizeof(req), STOW_MSG_HELLO, TEST_TAG);
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
    int len = test_build_simple(net_buf_tail(rx), net_buf_tailroom(rx), STOW_MSG_HELLO, TEST_TAG);
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
    net_buf_add(rx, test_build_simple(net_buf_tail(rx), net_buf_tailroom(rx), STOW_MSG_HELLO, TEST_TAG));
    endpoint->receive(endpoint, SESSION_A, rx);
    net_buf_unref(rx);
    test_sync();
    zassert_is_null(test_await_tx(K_MSEC(50)).buf, "closed session answered");
}

static void submit_hello(uint32_t session_id, uint32_t tag)
{
    uint8_t req[8];
    int len = test_build_simple(req, sizeof(req), STOW_MSG_HELLO, tag);
    zassert_equal(test_submit_rx(session_id, req, len), 0);
}

static void expect_hello(uint32_t tag)
{
    struct test_capture cap = test_await_tx(K_SECONDS(1));
    zassert_not_null(cap.buf, "no Hello response");
    uint32_t cmd = 0;
    uint32_t got_tag = 0;
    zcbor_state_t dec[4];
    zassert_true(test_decode_response_header(cap.buf, &cmd, &got_tag, dec, ARRAY_SIZE(dec)));
    zassert_equal(cmd, STOW_MSG_HELLO_RESPONSE);
    zassert_equal(got_tag, tag);
    net_buf_unref(cap.buf);
}

ZTEST(stow_protocol_transport, test_request_without_a_response_buffer_is_dropped)
{
    test_open_session(SESSION_A, STOW_ROLE_GUEST);
    hoard_tx_buffers();

    submit_hello(SESSION_A, 4);
    test_settle();
    release_tx_buffers();
    zassert_is_null(test_await_tx(K_MSEC(50)).buf, "a request was answered without a buffer");

    submit_hello(SESSION_A, 5);
    expect_hello(5);
}

ZTEST(stow_protocol_transport, test_request_is_dropped_when_queue_is_full)
{
    test_open_session(SESSION_A, STOW_ROLE_GUEST);

    test_block_sends(true);
    submit_hello(SESSION_A, 6);
    test_await_blocked_send();
    for (int i = 0; i < CONFIG_STOW_PROTOCOL_QUEUE_SIZE; i++)
    {
        submit_hello(SESSION_A, 7);
    }
    submit_hello(SESSION_A, 8);
    test_block_sends(false);

    expect_hello(6);
    for (int i = 0; i < CONFIG_STOW_PROTOCOL_QUEUE_SIZE; i++)
    {
        expect_hello(7);
    }
    test_sync();
    zassert_is_null(test_await_tx(K_MSEC(50)).buf, "a request beyond the queue was answered");
}

static void subscribe_test_int(uint32_t session_id)
{
    uint8_t req[16];
    int len = test_build_with_id(req, sizeof(req), STOW_MSG_SUBSCRIBE, TEST_TAG, STOW_ID_TEST_INT);
    zassert_equal(test_submit_rx(session_id, req, len), 0);
    struct test_capture cap = test_await_tx(K_SECONDS(1));
    zassert_not_null(cap.buf);
    zassert_equal(response_code(cap.buf), STOW_MSG_OK);
    net_buf_unref(cap.buf);
}

static void set_test_int(int32_t value)
{
    data_value_t v = { .type = STOW_ITEM_TYPE_INT, .data.int_value = value };
    zassert_equal(stow_set(STOW_ID_TEST_INT, v), 0);
}

static void expect_test_int_update(int32_t expected)
{
    struct test_capture cap = test_await_tx(K_SECONDS(1));
    zassert_not_null(cap.buf, "no Update");
    uint32_t cmd = 0;
    uint32_t item = 0;
    int32_t value = 0;
    zcbor_state_t dec[4];
    zassert_true(test_decode_response_header(cap.buf, &cmd, NULL, dec, ARRAY_SIZE(dec)));
    zassert_equal(cmd, STOW_MSG_UPDATE);
    zassert_true(zcbor_uint32_decode(dec, &item));
    zassert_true(zcbor_int32_decode(dec, &value));
    zassert_equal(item, STOW_ID_TEST_INT);
    zassert_equal(value, expected);
    net_buf_unref(cap.buf);
}

ZTEST(stow_protocol_transport, test_refused_update_is_dropped_and_not_retried)
{
    test_open_session(SESSION_A, STOW_ROLE_GUEST);
    subscribe_test_int(SESSION_A);

    test_refuse_sends(true);
    set_test_int(1);
    test_sync();
    test_refuse_sends(false);
    test_settle();
    zassert_is_null(test_await_tx(K_NO_WAIT).buf, "a refused Update was sent later");

    set_test_int(2);
    expect_test_int_update(2);
}

ZTEST(stow_protocol_transport, test_update_that_cannot_be_queued_is_dropped)
{
    test_open_session(SESSION_A, STOW_ROLE_GUEST);
    subscribe_test_int(SESSION_A);

    test_block_sends(true);
    submit_hello(SESSION_A, 9);
    test_await_blocked_send();
    for (int i = 0; i < CONFIG_STOW_PROTOCOL_QUEUE_SIZE; i++)
    {
        submit_hello(SESSION_A, 10);
    }
    set_test_int(3);
    test_block_sends(false);

    expect_hello(9);
    for (int i = 0; i < CONFIG_STOW_PROTOCOL_QUEUE_SIZE; i++)
    {
        expect_hello(10);
    }
    test_sync();
    zassert_is_null(test_await_tx(K_MSEC(50)).buf, "an Update that could not be queued was sent");

    set_test_int(4);
    expect_test_int_update(4);
}
