/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "test_common.h"

#define SESSION_A 21
#define SESSION_B 22

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

ZTEST_SUITE(stow_protocol_describe, NULL, NULL, before_each, after_each, NULL);

/**
 * @brief Submit a Describe request with start_id and decode the response.
 *
 * @param[in]  session_id     Session to send the request on
 * @param[in]  start_id       Item ID to start describing from
 * @param[out] out_chunk      Buffer to copy the chunk bytes into
 * @param[in]  out_chunk_size Size of out_chunk
 * @param[out] out_next_id    Populated with next_item_id from the response
 * @param[out] out_has_more   Populated with has_more from the response
 *
 * @return Chunk length on success, -1 on failure or unexpected response code
 */
static int request_describe_chunk(uint32_t session_id, uint32_t start_id, uint8_t* out_chunk, size_t out_chunk_size,
                                   uint32_t* out_next_id, bool* out_has_more)
{
    uint8_t req[16];
    int len = test_build_with_id(req, sizeof(req), STOW_MSG_DESCRIBE, start_id);
    zassert_true(len > 0);
    zassert_equal(test_submit_rx(session_id, STOW_ROLE_GUEST, req, len), 0);

    struct test_capture cap = test_await_tx(K_SECONDS(1));
    zassert_not_null(cap.buf);
    zassert_equal(cap.session_id, session_id);

    uint32_t resp_cmd;
    zcbor_state_t dec[4];
    zassert_true(test_decode_response_header(cap.buf, &resp_cmd, dec, ARRAY_SIZE(dec)));
    if (resp_cmd != STOW_MSG_DESCRIBE_RESPONSE)
    {
        net_buf_unref(cap.buf);
        return -1;
    }

    uint32_t next_id = 0;
    zassert_true(zcbor_uint32_decode(dec, &next_id));

    bool has_more = false;
    zassert_true(zcbor_bool_decode(dec, &has_more));

    struct zcbor_string chunk;
    zassert_true(zcbor_bstr_decode(dec, &chunk));
    zassert_true(chunk.len <= out_chunk_size);
    memcpy(out_chunk, chunk.value, chunk.len);
    int chunk_len = (int)chunk.len;

    net_buf_unref(cap.buf);

    *out_next_id = next_id;
    *out_has_more = has_more;
    return chunk_len;
}

ZTEST(stow_protocol_describe, test_describe_runs_to_completion)
{
    int ret = stow_protocol_session_open(SESSION_A, STOW_ROLE_GUEST);
    zassert_equal(ret, 0);

    uint8_t reassembled[2048] = { 0 };
    size_t total = 0;
    uint32_t iterations = 0;
    uint32_t next_id = 0;
    bool has_more = true;

    uint8_t chunk[CONFIG_STOW_PROTOCOL_DESCRIBE_CHUNK_SIZE];

    while (has_more)
    {
        int chunk_len = request_describe_chunk(SESSION_A, next_id, chunk, sizeof(chunk), &next_id, &has_more);
        zassert_true(chunk_len >= 0);
        zassert_true(total + (size_t)chunk_len <= sizeof(reassembled));
        memcpy(reassembled + total, chunk, chunk_len);
        total += chunk_len;
        iterations++;
        zassert_true(iterations < 64, "describe did not terminate");
    }

    // Verify we captured every item by decoding the reassembled stream as
    // back-to-back maps and counting.
    zcbor_state_t dec[4];
    zcbor_new_decode_state(dec, ARRAY_SIZE(dec), reassembled, total, STOW_ID_COUNT, NULL, 0);

    size_t items_seen = 0;
    while (items_seen < STOW_ID_COUNT)
    {
        if (!zcbor_map_start_decode(dec))
        {
            break;
        }
        for (int i = 0; i < 9 * 2; i++)
        {
            zassert_true(zcbor_any_skip(dec, NULL), "any_skip failed at item %zu pair %d", items_seen, i);
        }
        zassert_true(zcbor_map_end_decode(dec));
        items_seen++;
    }
    zassert_equal(items_seen, STOW_ID_COUNT, "describe missed items: saw %zu, expected %u", items_seen, STOW_ID_COUNT);
}

ZTEST(stow_protocol_describe, test_describe_returns_no_more_when_done)
{
    zassert_equal(stow_protocol_session_open(SESSION_A, STOW_ROLE_GUEST), 0);

    uint8_t chunk[CONFIG_STOW_PROTOCOL_DESCRIBE_CHUNK_SIZE];
    uint32_t next_id = 0;
    bool has_more = true;

    // Drive to completion.
    uint32_t iterations = 0;
    while (has_more)
    {
        int chunk_len = request_describe_chunk(SESSION_A, next_id, chunk, sizeof(chunk), &next_id, &has_more);
        zassert_true(chunk_len >= 0);
        zassert_true(++iterations < 64, "describe did not terminate");
    }

    // Once has_more is false the next_id must equal STOW_ID_COUNT.
    zassert_equal(next_id, STOW_ID_COUNT);
}

ZTEST(stow_protocol_describe, test_describe_two_sessions_independent)
{
    zassert_equal(stow_protocol_session_open(SESSION_A, STOW_ROLE_GUEST), 0);
    zassert_equal(stow_protocol_session_open(SESSION_B, STOW_ROLE_GUEST), 0);

    uint8_t chunk_a[CONFIG_STOW_PROTOCOL_DESCRIBE_CHUNK_SIZE];
    uint8_t chunk_b[CONFIG_STOW_PROTOCOL_DESCRIBE_CHUNK_SIZE];
    uint32_t next_a = 0, next_b = 0;
    bool more_a = false, more_b = false;

    // Both sessions request the first chunk from start_id=0.
    int len_a = request_describe_chunk(SESSION_A, 0, chunk_a, sizeof(chunk_a), &next_a, &more_a);
    int len_b = request_describe_chunk(SESSION_B, 0, chunk_b, sizeof(chunk_b), &next_b, &more_b);

    zassert_true(len_a > 0);
    zassert_true(len_b > 0);

    // Both responses must be identical since describe is stateless.
    zassert_equal(len_a, len_b);
    zassert_equal(next_a, next_b);
    zassert_equal(more_a, more_b);
    zassert_mem_equal(chunk_a, chunk_b, len_a);
}
