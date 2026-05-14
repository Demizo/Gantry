/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "test_common.h"

#define SESSION_ID 51

static void before_each(void* fixture)
{
    (void)fixture;
    test_reset();
}

static void after_each(void* fixture)
{
    (void)fixture;
    (void)stow_protocol_session_closed(SESSION_ID);
    test_sync();
    test_drain_captures();
}

ZTEST_SUITE(stow_protocol_netbuf, NULL, NULL, before_each, after_each, NULL);

ZTEST(stow_protocol_netbuf, test_response_has_reserved_headroom_and_tailroom)
{
    zassert_equal(stow_protocol_session_open(SESSION_ID, STOW_ROLE_GUEST), 0);

    uint8_t req[8];
    int len = test_build_simple(req, sizeof(req), STOW_MSG_VERSION);
    zassert_equal(test_submit_rx(SESSION_ID, STOW_ROLE_GUEST, req, len), 0);

    struct test_capture cap = test_await_tx(K_SECONDS(1));
    zassert_not_null(cap.buf);

    // Headroom is the gap between the start of the buf's data area and the
    // current `data` pointer. For a freshly allocated buf with
    // net_buf_reserve(buf, N) and no prior pushes, headroom == N.
    size_t headroom = net_buf_headroom(cap.buf);
    zassert_equal(headroom, TEST_TX_HEADROOM, "expected %u bytes of headroom, got %zu", TEST_TX_HEADROOM, headroom);

    // Tailroom must be at least the configured tailroom (the encoder is
    // required to leave room for it).
    size_t tailroom = net_buf_tailroom(cap.buf);
    zassert_true(tailroom >= TEST_TX_TAILROOM, "expected >= %u bytes of tailroom, got %zu", TEST_TX_TAILROOM, tailroom);

    net_buf_unref(cap.buf);
}

ZTEST(stow_protocol_netbuf, test_app_can_prepend_headers_and_append_footers)
{
    zassert_equal(stow_protocol_session_open(SESSION_ID, STOW_ROLE_GUEST), 0);

    uint8_t req[8];
    int len = test_build_simple(req, sizeof(req), STOW_MSG_VERSION);
    zassert_equal(test_submit_rx(SESSION_ID, STOW_ROLE_GUEST, req, len), 0);

    struct test_capture cap = test_await_tx(K_SECONDS(1));
    zassert_not_null(cap.buf);

    // Remember the original payload so we can verify it is intact afterwards.
    uint8_t payload_copy[64];
    size_t payload_len = cap.buf->len;
    zassert_true(payload_len <= sizeof(payload_copy));
    memcpy(payload_copy, cap.buf->data, payload_len);

    // Prepend an app-defined header
    uint8_t header[TEST_TX_HEADROOM];
    for (size_t i = 0; i < sizeof(header); i++)
    {
        header[i] = (uint8_t)(0xA0 + i);
    }
    uint8_t* hp = net_buf_push(cap.buf, sizeof(header));
    memcpy(hp, header, sizeof(header));

    // Append an app-defined footer
    uint8_t footer[TEST_TX_TAILROOM];
    for (size_t i = 0; i < sizeof(footer); i++)
    {
        footer[i] = (uint8_t)(0xF0 + i);
    }
    uint8_t* fp = net_buf_add(cap.buf, sizeof(footer));
    memcpy(fp, footer, sizeof(footer));

    // Verify resulting layout: [header][original payload][footer]
    zassert_equal(cap.buf->len, sizeof(header) + payload_len + sizeof(footer));
    zassert_mem_equal(cap.buf->data, header, sizeof(header));
    zassert_mem_equal(cap.buf->data + sizeof(header), payload_copy, payload_len);
    zassert_mem_equal(cap.buf->data + sizeof(header) + payload_len, footer, sizeof(footer));

    net_buf_unref(cap.buf);
}
