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

#define TEST_BUF_DATA_SIZE 320
#define TEST_BUF_USER_DATA 0

NET_BUF_POOL_DEFINE(test_tx_pool, 16, TEST_BUF_DATA_SIZE, TEST_BUF_USER_DATA, NULL);
NET_BUF_POOL_DEFINE(test_rx_pool, 16, TEST_BUF_DATA_SIZE, TEST_BUF_USER_DATA, NULL);

K_MSGQ_DEFINE(test_capture_msgq, sizeof(struct test_capture), TEST_MAX_PENDING_TX, sizeof(void*));

static bool g_protocol_initialized;

//**********************************************************
//* TX Callback
//**********************************************************

static void test_tx_cb(uint32_t session_id, struct net_buf* buf)
{
    struct test_capture cap = { .session_id = session_id, .buf = buf };
    if (k_msgq_put(&test_capture_msgq, &cap, K_NO_WAIT) != 0)
    {
        LOG_WRN("Test capture queue full — dropping TX for session %u", session_id);
        net_buf_unref(buf);
    }
}

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
    struct stow_protocol_config cfg = {
        .response_cb = test_tx_cb,
        .response_pool = &test_tx_pool,
        .headroom = TEST_TX_HEADROOM,
        .tailroom = TEST_TX_TAILROOM,
    };
    int ret = stow_protocol_init(&cfg);
    zassert_equal(ret, 0, "stow_protocol_init failed: %d", ret);
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
    // Issue a Version request against an unknown session. The protocol thread
    // produces an Error response; once we observe it, every event that was
    // queued ahead of our request has been processed.
    uint8_t buf[8];
    int len = test_build_simple(buf, sizeof(buf), STOW_MSG_VERSION);
    zassert_true(len > 0);

    int ret = test_submit_rx(0xDEADBEEFu, STOW_ROLE_GUEST, buf, (size_t)len);
    zassert_equal(ret, 0);

    struct test_capture cap = test_await_tx(K_SECONDS(1));
    if (cap.buf != NULL)
    {
        net_buf_unref(cap.buf);
    }
}

void test_reset(void)
{
    ensure_protocol_init();
    test_drain_captures();
}

//**********************************************************
//* RX Helpers
//**********************************************************

struct net_buf* test_alloc_rx(void) { return net_buf_alloc(&test_rx_pool, K_NO_WAIT); }

int test_submit_rx(uint32_t session_id, stow_role_t roles, const uint8_t* data, size_t len)
{
    struct net_buf* buf = test_alloc_rx();
    if (buf == NULL)
    {
        return -ENOMEM;
    }
    void* dest = net_buf_add(buf, len);
    memcpy(dest, data, len);

    int ret = stow_protocol_handle_rx(session_id, roles, buf);
    net_buf_unref(buf);
    return ret;
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

int test_build_multi_get(uint8_t* out_buf, size_t out_buf_size, const uint32_t* ids, uint8_t count)
{
    zcbor_state_t enc[4];
    zcbor_new_encode_state(enc, ARRAY_SIZE(enc), out_buf, out_buf_size, 1);
    uint32_t elem_count = 1u + (uint32_t)count;
    if (!zcbor_list_start_encode(enc, elem_count) || !zcbor_uint32_put(enc, STOW_MSG_MULTI_GET))
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

int test_build_multi_set_ints(
    uint8_t* out_buf, size_t out_buf_size, uint32_t id1, int32_t val1, uint32_t id2, int32_t val2)
{
    zcbor_state_t enc[4];
    zcbor_new_encode_state(enc, ARRAY_SIZE(enc), out_buf, out_buf_size, 1);
    if (!zcbor_list_start_encode(enc, 5) || !zcbor_uint32_put(enc, STOW_MSG_MULTI_SET) || !zcbor_uint32_put(enc, id1) ||
        !zcbor_int32_put(enc, val1) || !zcbor_uint32_put(enc, id2) || !zcbor_int32_put(enc, val2) ||
        !zcbor_list_end_encode(enc, 5))
    {
        return -ENOMEM;
    }
    return (int)(enc[0].payload - out_buf);
}
