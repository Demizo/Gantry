/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "test_common.h"

#include <gantry/error.h>
#include <gantry/event.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(test_common, 3);

//**********************************************************
//* Pools
//**********************************************************

#define TEST_BUF_USER_DATA 0

#define TEST_SYNC_SESSION 0xDEADBEEFu

NET_BUF_POOL_DEFINE(test_tx_pool, 16, TEST_BUF_DATA_SIZE, TEST_BUF_USER_DATA, NULL);
NET_BUF_POOL_DEFINE(test_rx_pool, 16, TEST_BUF_DATA_SIZE, TEST_BUF_USER_DATA, NULL);

K_MSGQ_DEFINE(test_capture_msgq, sizeof(struct test_capture), TEST_MAX_PENDING_TX, sizeof(void*));
K_MSGQ_DEFINE(test_sync_msgq, sizeof(struct net_buf*), 1, sizeof(void*));

static bool g_protocol_initialized;

static atomic_t g_refuse_sends;

//**********************************************************
//* Fake Transport
//**********************************************************

static int test_send(struct stow_transport* transport, uint32_t session_id, struct net_buf* buf)
{
    ARG_UNUSED(transport);

    if (session_id == TEST_SYNC_SESSION)
    {
        return k_msgq_put(&test_sync_msgq, &buf, K_NO_WAIT) == 0 ? 0 : -EIO;
    }

    if (atomic_get(&g_refuse_sends) != 0)
    {
        return -ENOBUFS;
    }

    struct test_capture cap = { .session_id = session_id, .buf = buf };
    if (k_msgq_put(&test_capture_msgq, &cap, K_NO_WAIT) != 0)
    {
        LOG_WRN("Test capture queue full, refusing TX for session %u", session_id);
        return -ENOBUFS;
    }
    return 0;
}

struct stow_transport test_transport = {
    .send = test_send,
    .pool = &test_tx_pool,
    .headroom = TEST_TX_HEADROOM,
    .tailroom = TEST_TX_TAILROOM,
};

void test_refuse_sends(bool refuse) { atomic_set(&g_refuse_sends, refuse ? 1 : 0); }

//**********************************************************
//* Lifecycle
//**********************************************************

static void ensure_protocol_init(void)
{
    if (g_protocol_initialized)
    {
        return;
    }
    stow_init();
    int ret = stow_protocol_init();
    zassert_equal(ret, 0, "stow_protocol_init failed: %d", ret);
    ret = test_endpoint_open(TEST_SYNC_SESSION, STOW_ROLE_GUEST, &test_transport);
    zassert_equal(ret, 0, "sync session open failed: %d", ret);
    g_protocol_initialized = true;
}

void test_drain_captures(void)
{
    struct test_capture cap;
    while (k_msgq_get(&test_capture_msgq, &cap, K_NO_WAIT) == 0)
    {
        if (cap.buf != NULL)
        {
            net_buf_unref(cap.buf);
        }
    }
}

void test_sync(void)
{
    uint8_t buf[8];
    int len = test_build_simple(buf, sizeof(buf), STOW_MSG_HELLO);
    zassert_true(len > 0);

    k_tid_t self = k_current_get();
    int priority = k_thread_priority_get(self);
    k_thread_priority_set(self, CONFIG_STOW_PROTOCOL_THREAD_PRIORITY + 1);

    int ret = test_submit_rx(TEST_SYNC_SESSION, buf, (size_t)len);
    struct net_buf* response = NULL;
    int got = k_msgq_get(&test_sync_msgq, &response, K_SECONDS(1));
    k_thread_priority_set(self, priority);

    zassert_equal(ret, 0);
    zassert_equal(got, 0, "sync response missing");
    net_buf_unref(response);
}

void test_reset(void)
{
    ensure_protocol_init();
    test_refuse_sends(false);
    test_drain_captures();
}

int test_endpoint_open(uint32_t session_id, stow_role_t roles, struct stow_transport* transport)
{
    return stow_protocol_endpoint.session_opened(&stow_protocol_endpoint, session_id, roles, transport);
}

void test_endpoint_close(uint32_t session_id)
{
    stow_protocol_endpoint.session_closed(&stow_protocol_endpoint, session_id);
}

void test_open_session(uint32_t session_id, stow_role_t roles)
{
    int ret = test_endpoint_open(session_id, roles, &test_transport);
    zassert_equal(ret, 0, "session_open(%u) failed: %d", session_id, ret);
}

//**********************************************************
//* RX Helpers
//**********************************************************

struct net_buf* test_alloc_rx(void) { return net_buf_alloc(&test_rx_pool, K_NO_WAIT); }

int test_submit_rx(uint32_t session_id, const uint8_t* data, size_t len)
{
    struct net_buf* buf = test_alloc_rx();
    if (buf == NULL)
    {
        return -ENOMEM;
    }
    void* dest = net_buf_add(buf, len);
    memcpy(dest, data, len);

    stow_protocol_endpoint.receive(&stow_protocol_endpoint, session_id, buf);
    net_buf_unref(buf);
    return SUCCESS;
}

//**********************************************************
//* TX Helpers
//**********************************************************

struct test_capture test_await_tx(k_timeout_t timeout)
{
    struct test_capture cap = { 0 };
    if (k_msgq_get(&test_capture_msgq, &cap, timeout) != 0)
    {
        return (struct test_capture){ 0 };
    }
    return cap;
}

bool test_decode_response_header(struct net_buf* buf, uint32_t* out_cmd, zcbor_state_t* dec, size_t dec_states)
{
    zcbor_new_decode_state(dec, dec_states, buf->data, buf->len, 1, NULL, 0);
    if (!zcbor_list_start_decode(dec))
    {
        return false;
    }
    if (!zcbor_uint32_decode(dec, out_cmd))
    {
        return false;
    }
    return true;
}

//**********************************************************
//* Request Builders
//**********************************************************

int test_build_simple(uint8_t* out_buf, size_t out_buf_size, uint32_t cmd)
{
    zcbor_state_t enc[4];
    zcbor_new_encode_state(enc, ARRAY_SIZE(enc), out_buf, out_buf_size, 1);
    if (!zcbor_list_start_encode(enc, 1) || !zcbor_uint32_put(enc, cmd) || !zcbor_list_end_encode(enc, 1))
    {
        return -ENOMEM;
    }
    return (int)(enc[0].payload - out_buf);
}

int test_build_with_id(uint8_t* out_buf, size_t out_buf_size, uint32_t cmd, uint32_t item_id)
{
    zcbor_state_t enc[4];
    zcbor_new_encode_state(enc, ARRAY_SIZE(enc), out_buf, out_buf_size, 1);
    if (!zcbor_list_start_encode(enc, 2) || !zcbor_uint32_put(enc, cmd) || !zcbor_uint32_put(enc, item_id) ||
        !zcbor_list_end_encode(enc, 2))
    {
        return -ENOMEM;
    }
    return (int)(enc[0].payload - out_buf);
}

int test_build_set_int(uint8_t* out_buf, size_t out_buf_size, uint32_t item_id, int32_t value)
{
    zcbor_state_t enc[4];
    zcbor_new_encode_state(enc, ARRAY_SIZE(enc), out_buf, out_buf_size, 1);
    if (!zcbor_list_start_encode(enc, 3) || !zcbor_uint32_put(enc, STOW_MSG_SET) || !zcbor_uint32_put(enc, item_id) ||
        !zcbor_int32_put(enc, value) || !zcbor_list_end_encode(enc, 3))
    {
        return -ENOMEM;
    }
    return (int)(enc[0].payload - out_buf);
}

int test_build_set_string(uint8_t* out_buf, size_t out_buf_size, uint32_t item_id, const char* value)
{
    zcbor_state_t enc[4];
    zcbor_new_encode_state(enc, ARRAY_SIZE(enc), out_buf, out_buf_size, 1);
    struct zcbor_string s = { .value = value, .len = strlen(value) };
    if (!zcbor_list_start_encode(enc, 3) || !zcbor_uint32_put(enc, STOW_MSG_SET) || !zcbor_uint32_put(enc, item_id) ||
        !zcbor_tstr_encode(enc, &s) || !zcbor_list_end_encode(enc, 3))
    {
        return -ENOMEM;
    }
    return (int)(enc[0].payload - out_buf);
}

int test_build_list(uint8_t* out_buf, size_t out_buf_size, uint32_t cmd, const uint32_t* ids, uint8_t count)
{
    zcbor_state_t enc[4];
    zcbor_new_encode_state(enc, ARRAY_SIZE(enc), out_buf, out_buf_size, 1);
    uint32_t elem_count = 1u + (uint32_t)count;
    if (!zcbor_list_start_encode(enc, elem_count) || !zcbor_uint32_put(enc, cmd))
    {
        return -ENOMEM;
    }
    for (uint8_t i = 0; i < count; i++)
    {
        if (!zcbor_uint32_put(enc, ids[i]))
        {
            return -ENOMEM;
        }
    }
    if (!zcbor_list_end_encode(enc, elem_count))
    {
        return -ENOMEM;
    }
    return (int)(enc[0].payload - out_buf);
}

int test_build_set_ints(uint8_t* out_buf, size_t out_buf_size, uint32_t id1, int32_t val1, uint32_t id2, int32_t val2)
{
    zcbor_state_t enc[4];
    zcbor_new_encode_state(enc, ARRAY_SIZE(enc), out_buf, out_buf_size, 1);
    if (!zcbor_list_start_encode(enc, 5) || !zcbor_uint32_put(enc, STOW_MSG_SET) || !zcbor_uint32_put(enc, id1) ||
        !zcbor_int32_put(enc, val1) || !zcbor_uint32_put(enc, id2) || !zcbor_int32_put(enc, val2) ||
        !zcbor_list_end_encode(enc, 5))
    {
        return -ENOMEM;
    }
    return (int)(enc[0].payload - out_buf);
}
