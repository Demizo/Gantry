/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "test_common.h"

#define SESSION_ID 81

static void before_each(void* fixture)
{
    (void)fixture;
    test_reset();
    test_open_session(SESSION_ID, STOW_ROLE_GUEST);
}

static void after_each(void* fixture)
{
    (void)fixture;
    test_endpoint_close(SESSION_ID);
    test_sync();
    test_drain_captures();
    stow_init();
}

ZTEST_SUITE(stow_protocol_errors, NULL, NULL, before_each, after_each, NULL);

static struct test_error exchange_error(const uint8_t* request, size_t len)
{
    zassert_equal(test_submit_rx(SESSION_ID, request, len), 0);
    struct test_capture cap = test_await_tx(K_SECONDS(1));
    zassert_not_null(cap.buf, "no response");
    zassert_equal(cap.session_id, SESSION_ID);
    struct test_error error;
    zassert_true(test_decode_error(cap.buf, &error), "response is not an Error");
    net_buf_unref(cap.buf);
    return error;
}

ZTEST(stow_protocol_errors, test_oversized_response_is_too_large)
{
    char long_value[351];
    memset(long_value, 'x', sizeof(long_value) - 1);
    long_value[sizeof(long_value) - 1] = '\0';
    data_value_t value = { .type = STOW_ITEM_TYPE_STRING, .data.string_value = long_value };
    zassert_equal(stow_set(STOW_ID_TEST_LONG_STRING, value), 0);

    uint8_t req[16];
    int len = test_build_with_id(req, sizeof(req), STOW_MSG_GET, TEST_TAG, STOW_ID_TEST_LONG_STRING);
    struct test_error error = exchange_error(req, len);
    zassert_equal(error.code, STOW_ERR_TOO_LARGE);
    zassert_equal(error.tag, TEST_TAG);
    zassert_equal(error.item, STOW_ID_TEST_LONG_STRING);
}

ZTEST(stow_protocol_errors, test_set_allocation_failure_returns_out_of_memory)
{
    uint8_t req[16];
    zcbor_state_t enc[4];
    zcbor_new_encode_state(enc, ARRAY_SIZE(enc), req, sizeof(req), 1);
    const uint8_t payload[] = { 0x01, 0x02, 0x03 };
    struct zcbor_string bstr = { .value = payload, .len = sizeof(payload) };
    zassert_true(
        zcbor_list_start_encode(enc, 4) && zcbor_uint32_put(enc, STOW_MSG_SET) && zcbor_uint32_put(enc, TEST_TAG) &&
        zcbor_uint32_put(enc, STOW_ID_TEST_BUFFER) && zcbor_bstr_encode(enc, &bstr) && zcbor_list_end_encode(enc, 4));
    size_t len = enc[0].payload - req;

    void* blocks[256] = { 0 };
    size_t block_count = 0;
    while (block_count < ARRAY_SIZE(blocks) && mem_alloc(1, &blocks[block_count]) == 0)
    {
        block_count++;
    }
    zassert_true(block_count < ARRAY_SIZE(blocks), "memory pools were not exhausted");
    zassert_true(block_count >= 2);
    mem_unref(&blocks[--block_count]);
    mem_unref(&blocks[--block_count]);

    zassert_equal(test_submit_rx(SESSION_ID, req, len), 0);
    struct test_capture cap = test_await_tx(K_SECONDS(1));

    for (size_t i = 0; i < block_count; i++)
    {
        mem_unref(&blocks[i]);
    }

    zassert_not_null(cap.buf);
    uint32_t cmd;
    zcbor_state_t dec[4];
    zassert_true(test_decode_response_header(cap.buf, &cmd, NULL, dec, ARRAY_SIZE(dec)));
    zassert_equal(cmd, STOW_MSG_ERROR, "expected Error (got %u)", cmd);
    uint32_t code;
    zassert_true(zcbor_uint32_decode(dec, &code));
    zassert_equal(code, STOW_ERR_OUT_OF_MEMORY);
    net_buf_unref(cap.buf);
}
