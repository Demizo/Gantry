/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gantry/stow/stow_describe.h>
#include <zephyr/fatal.h>

#include "test_common.h"

#define SESSION_A 21
#define SESSION_B 22
#define SESSION_SMALL 23

#define SMALL_BUF_SIZE 48
#define NARROW_BUF_SIZE 160

NET_BUF_POOL_DEFINE(small_pool, 2, SMALL_BUF_SIZE, 0, NULL);
NET_BUF_POOL_DEFINE(narrow_pool, 4, NARROW_BUF_SIZE, 0, NULL);

static struct stow_transport small_transport = {
    .send = NULL,
    .pool = &small_pool,
    .headroom = 0U,
    .tailroom = 0U,
};

static struct stow_transport narrow_transport = {
    .send = NULL,
    .pool = &narrow_pool,
    .headroom = 0U,
    .tailroom = 0U,
};

static atomic_t g_expect_assert;
static atomic_t g_asserts;

void k_sys_fatal_error_handler(unsigned int reason, const struct arch_esf* esf)
{
    ARG_UNUSED(esf);
    if (atomic_get(&g_expect_assert) == 0)
    {
        k_fatal_halt(reason);
    }
    atomic_inc(&g_asserts);
}

static void before_each(void* fixture)
{
    (void)fixture;
    test_reset();
    small_transport.send = test_transport.send;
}

static void after_each(void* fixture)
{
    (void)fixture;
    test_endpoint_close(SESSION_A);
    test_endpoint_close(SESSION_B);
    test_endpoint_close(SESSION_SMALL);
    test_sync();
    test_drain_captures();
}

ZTEST_SUITE(stow_protocol_describe, NULL, NULL, before_each, after_each, NULL);

/**
 * @brief Encode a Describe request, filtered by item names when @p names is not NULL
 *
 * @return Encoded length
 */
static int build_describe(
    uint8_t* out_buf, size_t out_buf_size, uint32_t start_id, const char* const* names, size_t name_count)
{
    zcbor_state_t enc[4];
    zcbor_new_encode_state(enc, ARRAY_SIZE(enc), out_buf, out_buf_size, 1);
    zassert_true(zcbor_list_start_encode(enc, 4));
    zassert_true(zcbor_uint32_put(enc, STOW_MSG_DESCRIBE));
    zassert_true(zcbor_uint32_put(enc, TEST_TAG));
    zassert_true(zcbor_uint32_put(enc, start_id));
    if (names != NULL)
    {
        zassert_true(zcbor_list_start_encode(enc, name_count));
        for (size_t i = 0; i < name_count; i++)
        {
            struct zcbor_string name = { .value = names[i], .len = strlen(names[i]) };
            zassert_true(zcbor_tstr_encode(enc, &name));
        }
        zassert_true(zcbor_list_end_encode(enc, name_count));
    }
    zassert_true(zcbor_list_end_encode(enc, 4));
    return (int)(enc[0].payload - out_buf);
}

/**
 * @brief Submit a Describe request, optionally filtered by item names, and decode the response.
 *
 * @param[in]  session_id         Session to send the request on
 * @param[in]  start_id           Item ID to start describing from
 * @param[in]  names              Item names to describe, or NULL for every item
 * @param[in]  name_count         Number of entries in @p names
 * @param[out] out_chunk          Buffer to copy the chunk bytes into
 * @param[in]  out_chunk_size     Size of out_chunk
 * @param[out] out_next_id        Populated with next_item_id from the response, or the item count when it is null
 * @param[out] out_more_remaining Populated with whether next_item_id is present
 *
 * @return Chunk length on success, -1 on failure or unexpected response code
 */
static int request_filtered_chunk(
    uint32_t session_id, uint32_t start_id, const char* const* names, size_t name_count, uint8_t* out_chunk,
    size_t out_chunk_size, uint32_t* out_next_id, bool* out_more_remaining)
{
    uint8_t req[128];
    int len = build_describe(req, sizeof(req), start_id, names, name_count);
    zassert_equal(test_submit_rx(session_id, req, len), 0);

    struct test_capture cap = test_await_tx(K_SECONDS(1));
    zassert_not_null(cap.buf);
    zassert_equal(cap.session_id, session_id);

    uint32_t resp_cmd;
    zcbor_state_t dec[4];
    zassert_true(test_decode_response_header(cap.buf, &resp_cmd, NULL, dec, ARRAY_SIZE(dec)));
    if (resp_cmd != STOW_MSG_DESCRIBE_RESPONSE)
    {
        net_buf_unref(cap.buf);
        return -1;
    }

    uint32_t next_id = STOW_ID_COUNT;
    bool more_remaining = zcbor_uint32_decode(dec, &next_id);
    if (!more_remaining)
    {
        zassert_true(zcbor_nil_expect(dec, NULL));
    }

    struct zcbor_string chunk;
    zassert_true(zcbor_bstr_decode(dec, &chunk));
    zassert_true(chunk.len <= out_chunk_size);
    memcpy(out_chunk, chunk.value, chunk.len);
    int chunk_len = (int)chunk.len;

    net_buf_unref(cap.buf);

    *out_next_id = next_id;
    *out_more_remaining = more_remaining;
    return chunk_len;
}

/**
 * @brief Submit an unfiltered Describe request and decode the response.
 */
static int request_describe_chunk(
    uint32_t session_id, uint32_t start_id, uint8_t* out_chunk, size_t out_chunk_size, uint32_t* out_next_id,
    bool* out_more_remaining)
{
    return request_filtered_chunk(
        session_id, start_id, NULL, 0, out_chunk, out_chunk_size, out_next_id, out_more_remaining);
}

/**
 * @brief Count the whole item descriptions in one chunk, which must parse on its own
 *
 * @param chunk Chunk bytes
 * @param len Chunk length
 * @param[out] ids Populated with the described item IDs
 *
 * @return Number of item descriptions in the chunk
 */
static size_t parse_chunk(const uint8_t* chunk, size_t len, uint32_t* ids)
{
    const uint8_t* start = chunk;
    size_t body_len = len;

    zcbor_state_t dec[4];
    zcbor_new_decode_state(dec, ARRAY_SIZE(dec), start, body_len, STOW_ID_COUNT, NULL, 0);

    size_t count = 0;
    while (dec[0].payload < start + body_len)
    {
        zassert_true(zcbor_map_start_decode(dec), "chunk holds a partial item");
        uint32_t key;
        zassert_true(zcbor_uint32_decode(dec, &key));
        zassert_equal(key, STOW_DESCRIBE_ID);
        zassert_true(zcbor_uint32_decode(dec, &ids[count]));
        for (int i = 0; i < 8 * 2; i++)
        {
            zassert_true(zcbor_any_skip(dec, NULL));
        }
        zassert_true(zcbor_map_end_decode(dec));
        count++;
    }
    return count;
}

ZTEST(stow_protocol_describe, test_describe_runs_to_completion)
{
    int ret = test_endpoint_open(SESSION_A, STOW_ROLE_GUEST, &test_transport);
    zassert_equal(ret, 0);

    uint8_t reassembled[2048] = { 0 };
    size_t total = 0;
    uint32_t iterations = 0;
    uint32_t next_id = 0;
    bool more_remaining = true;

    uint8_t chunk[TEST_BUF_DATA_SIZE];

    while (more_remaining)
    {
        int chunk_len = request_describe_chunk(SESSION_A, next_id, chunk, sizeof(chunk), &next_id, &more_remaining);
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
    test_open_session(SESSION_A, STOW_ROLE_GUEST);

    uint8_t chunk[TEST_BUF_DATA_SIZE];
    uint32_t next_id = 0;
    bool more_remaining = true;

    // Drive to completion.
    uint32_t iterations = 0;
    while (more_remaining)
    {
        int chunk_len = request_describe_chunk(SESSION_A, next_id, chunk, sizeof(chunk), &next_id, &more_remaining);
        zassert_true(chunk_len >= 0);
        zassert_true(++iterations < 64, "describe did not terminate");
    }

    // Once more_remaining is false the next_id must equal STOW_ID_COUNT.
    zassert_equal(next_id, STOW_ID_COUNT);
}

ZTEST(stow_protocol_describe, test_describe_two_sessions_independent)
{
    test_open_session(SESSION_A, STOW_ROLE_GUEST);
    test_open_session(SESSION_B, STOW_ROLE_GUEST);

    uint8_t chunk_a[TEST_BUF_DATA_SIZE];
    uint8_t chunk_b[TEST_BUF_DATA_SIZE];
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

ZTEST(stow_protocol_describe, test_every_chunk_holds_whole_items)
{
    test_open_session(SESSION_A, STOW_ROLE_GUEST);

    uint8_t chunk[TEST_BUF_DATA_SIZE];
    uint32_t ids[STOW_ID_COUNT];
    uint32_t next_id = 0;
    bool more_remaining = true;
    size_t total = 0;
    uint32_t iterations = 0;

    while (more_remaining)
    {
        uint32_t start = next_id;
        int len = request_describe_chunk(SESSION_A, start, chunk, sizeof(chunk), &next_id, &more_remaining);
        zassert_true(len > 0);
        size_t count = parse_chunk(chunk, len, &ids[total]);
        zassert_true(count > 0, "chunk made no progress");
        zassert_equal(ids[total], start, "chunk does not start at the requested item");
        total += count;
        zassert_true(++iterations < 64, "describe did not terminate");
    }

    zassert_equal(total, STOW_ID_COUNT);
}

ZTEST(stow_protocol_describe, test_filter_returns_only_named_items)
{
    test_open_session(SESSION_A, STOW_ROLE_GUEST);

    const char* const names[] = { "NoSuchItem", "TestDevInt" };
    uint8_t chunk[TEST_BUF_DATA_SIZE];
    uint32_t next_id = 0;
    bool more_remaining = true;
    int len =
        request_filtered_chunk(SESSION_A, 0, names, ARRAY_SIZE(names), chunk, sizeof(chunk), &next_id, &more_remaining);
    zassert_true(len > 0);
    zassert_false(more_remaining);
    zassert_equal(next_id, STOW_ID_COUNT);

    uint32_t ids[STOW_ID_COUNT];
    zassert_equal(parse_chunk(chunk, len, ids), 1);
    zassert_equal(ids[0], STOW_ID_TEST_DEV_INT);
}

ZTEST(stow_protocol_describe, test_filter_with_no_known_names_is_empty)
{
    test_open_session(SESSION_A, STOW_ROLE_GUEST);

    const char* const names[] = { "NoSuchItem" };
    uint8_t chunk[TEST_BUF_DATA_SIZE];
    uint32_t next_id = 0;
    bool more_remaining = true;
    int len =
        request_filtered_chunk(SESSION_A, 0, names, ARRAY_SIZE(names), chunk, sizeof(chunk), &next_id, &more_remaining);
    zassert_equal(len, 0);
    zassert_false(more_remaining);
}

ZTEST(stow_protocol_describe, test_filter_chunks_over_matching_items)
{
    narrow_transport.send = test_transport.send;
    zassert_equal(test_endpoint_open(SESSION_A, STOW_ROLE_GUEST, &narrow_transport), 0);

    const char* const names[] = { "StowHash", "TestSessionInt", "TestLongString" };
    uint8_t chunk[TEST_BUF_DATA_SIZE];
    uint32_t ids[STOW_ID_COUNT];
    uint32_t next_id = 0;
    bool more_remaining = true;
    size_t total = 0;
    uint32_t iterations = 0;

    while (more_remaining)
    {
        int len = request_filtered_chunk(
            SESSION_A, next_id, names, ARRAY_SIZE(names), chunk, sizeof(chunk), &next_id, &more_remaining);
        zassert_true(len > 0);
        total += parse_chunk(chunk, len, &ids[total]);
        zassert_true(
            !more_remaining || next_id == STOW_ID_TEST_SESSION_INT || next_id == STOW_ID_TEST_LONG_STRING,
            "next_item_id %u is not a matching item", next_id);
        zassert_true(++iterations < 8, "describe did not terminate");
    }

    zassert_true(iterations > 1, "filtered describe fit in one chunk");
    zassert_equal(total, 3);
    zassert_equal(ids[0], STOW_ID_STOW_HASH);
    zassert_equal(ids[1], STOW_ID_TEST_SESSION_INT);
    zassert_equal(ids[2], STOW_ID_TEST_LONG_STRING);
}

ZTEST(stow_protocol_describe, test_session_on_too_small_a_transport_asserts)
{
    atomic_clear(&g_asserts);
    atomic_set(&g_expect_assert, 1);
    int ret = test_endpoint_open(SESSION_SMALL, STOW_ROLE_GUEST, &small_transport);
    test_sync();
    atomic_clear(&g_expect_assert);

    zassert_equal(ret, 0);
    zassert_true(atomic_get(&g_asserts) > 0, "no description was found too large");
}

ZTEST(stow_protocol_describe, test_session_on_a_large_enough_transport_does_not_assert)
{
    atomic_clear(&g_asserts);
    atomic_set(&g_expect_assert, 1);
    int ret = test_endpoint_open(SESSION_A, STOW_ROLE_GUEST, &test_transport);
    test_sync();
    atomic_clear(&g_expect_assert);

    zassert_equal(ret, 0);
    zassert_equal(atomic_get(&g_asserts), 0);
}

ZTEST(stow_protocol_describe, test_item_too_large_for_an_empty_response_is_too_large)
{
    atomic_set(&g_expect_assert, 1);
    zassert_equal(test_endpoint_open(SESSION_SMALL, STOW_ROLE_GUEST, &small_transport), 0);
    test_sync();
    atomic_clear(&g_expect_assert);

    uint8_t req[16];
    int len = build_describe(req, sizeof(req), 0, NULL, 0);
    zassert_equal(test_submit_rx(SESSION_SMALL, req, len), 0);

    struct test_capture cap = test_await_tx(K_SECONDS(1));
    zassert_not_null(cap.buf);
    struct stow_serde_error error;
    zassert_true(test_decode_error(cap.buf, &error));
    zassert_equal(error.code, STOW_ERR_TOO_LARGE);
    zassert_equal(error.tag, TEST_TAG);
    zassert_equal(error.item, 0);
    net_buf_unref(cap.buf);
}
