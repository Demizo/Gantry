/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gantry/stow/stow_cobs_transport.h>

#include "test_common.h"

#define LINK_SESSION 71
#define LINK_MESSAGE_SIZE 240

NET_BUF_POOL_DEFINE(link_message_pool, 8, LINK_MESSAGE_SIZE, 0, NULL);
NET_BUF_POOL_DEFINE(link_frame_pool, 4, COBS_ENCODE_MAX_SIZE(LINK_MESSAGE_SIZE), 0, NULL);

K_MSGQ_DEFINE(link_frame_msgq, sizeof(struct net_buf*), 4, sizeof(void*));

static atomic_t g_refuse_writes;

static struct stow_cobs_transport link;

static const uint8_t request_frame[] = { 0x04, 0x83, 0x08, 0x07, 0x01, 0x00 };
static const uint8_t response_frame[] = { 0x04, 0x82, 0x0A, 0x07, 0x00 };

static int write_frame(struct net_buf* frame, void* user_data)
{
    ARG_UNUSED(user_data);
    if (atomic_get(&g_refuse_writes) != 0)
    {
        return -ENOBUFS;
    }
    return k_msgq_put(&link_frame_msgq, &frame, K_NO_WAIT) == 0 ? 0 : -ENOBUFS;
}

static void* setup(void)
{
    test_reset();
    struct stow_cobs_transport_config config = {
        .endpoint = &stow_protocol_endpoint,
        .session_id = LINK_SESSION,
        .roles = STOW_ROLE_GUEST,
        .message_pool = &link_message_pool,
        .frame_pool = &link_frame_pool,
        .write = write_frame,
        .user_data = NULL,
    };
    zassert_equal(stow_cobs_transport_init(&link, &config), 0);
    return NULL;
}

static void before_each(void* fixture)
{
    (void)fixture;
    test_reset();
    atomic_clear(&g_refuse_writes);
}

static void after_each(void* fixture)
{
    (void)fixture;
    atomic_clear(&g_refuse_writes);
    stow_cobs_transport_close(&link);
    test_sync();

    struct net_buf* frame = NULL;
    while (k_msgq_get(&link_frame_msgq, &frame, K_NO_WAIT) == 0)
    {
        net_buf_unref(frame);
    }
}

ZTEST_SUITE(stow_cobs_transport, NULL, setup, before_each, after_each, NULL);

static void expect_frame(const uint8_t* expected, size_t len)
{
    struct net_buf* frame = NULL;
    zassert_equal(k_msgq_get(&link_frame_msgq, &frame, K_SECONDS(1)), 0, "no frame written");
    zassert_equal(frame->len, len, "frame length %u", frame->len);
    zassert_mem_equal(frame->data, expected, len);
    net_buf_unref(frame);
}

ZTEST(stow_cobs_transport, test_frame_round_trip)
{
    zassert_equal(stow_cobs_transport_open(&link), 0);
    zassert_equal(stow_cobs_transport_feed(&link, request_frame, sizeof(request_frame)), 0);
    expect_frame(response_frame, sizeof(response_frame));
}

ZTEST(stow_cobs_transport, test_frame_split_across_feeds)
{
    zassert_equal(stow_cobs_transport_open(&link), 0);
    for (size_t i = 0; i < sizeof(request_frame); i++)
    {
        zassert_equal(stow_cobs_transport_feed(&link, &request_frame[i], 1), 0);
    }
    expect_frame(response_frame, sizeof(response_frame));
}

static void expect_no_frame(const char* why)
{
    test_sync();
    struct net_buf* frame = NULL;
    zassert_not_equal(k_msgq_get(&link_frame_msgq, &frame, K_MSEC(50)), 0, "%s", why);
}

ZTEST(stow_cobs_transport, test_refused_write_drops_the_response)
{
    zassert_equal(stow_cobs_transport_open(&link), 0);
    atomic_set(&g_refuse_writes, 1);

    zassert_equal(stow_cobs_transport_feed(&link, request_frame, sizeof(request_frame)), 0);
    expect_no_frame("refused frame was written");

    atomic_clear(&g_refuse_writes);
    expect_no_frame("refused frame was written later");

    zassert_equal(stow_cobs_transport_feed(&link, request_frame, sizeof(request_frame)), 0);
    expect_frame(response_frame, sizeof(response_frame));
}

ZTEST(stow_cobs_transport, test_full_frame_pool_drops_the_response)
{
    zassert_equal(stow_cobs_transport_open(&link), 0);

    struct net_buf* hoard[4];
    size_t hoarded = 0;
    while (hoarded < ARRAY_SIZE(hoard) && (hoard[hoarded] = net_buf_alloc(&link_frame_pool, K_NO_WAIT)) != NULL)
    {
        hoarded++;
    }

    zassert_equal(stow_cobs_transport_feed(&link, request_frame, sizeof(request_frame)), 0);
    expect_no_frame("frame written without a frame buffer");

    for (size_t i = 0; i < hoarded; i++)
    {
        net_buf_unref(hoard[i]);
    }

    zassert_equal(stow_cobs_transport_feed(&link, request_frame, sizeof(request_frame)), 0);
    expect_frame(response_frame, sizeof(response_frame));
}

ZTEST(stow_cobs_transport, test_reopen_discards_partial_frame)
{
    zassert_equal(stow_cobs_transport_open(&link), 0);
    zassert_equal(stow_cobs_transport_feed(&link, request_frame, 2), 0);
    stow_cobs_transport_close(&link);
    test_sync();

    zassert_equal(stow_cobs_transport_open(&link), 0);
    zassert_equal(stow_cobs_transport_feed(&link, request_frame, sizeof(request_frame)), 0);
    expect_frame(response_frame, sizeof(response_frame));
}

ZTEST(stow_cobs_transport, test_closed_link_writes_nothing)
{
    zassert_equal(stow_cobs_transport_open(&link), 0);
    stow_cobs_transport_close(&link);
    test_sync();

    zassert_equal(stow_cobs_transport_feed(&link, request_frame, sizeof(request_frame)), 0);
    test_sync();
    struct net_buf* frame = NULL;
    zassert_not_equal(k_msgq_get(&link_frame_msgq, &frame, K_MSEC(50)), 0, "closed link wrote a frame");
}

static size_t build_oversized_frame(uint8_t* out)
{
    const size_t message_len = LINK_MESSAGE_SIZE + 1U;
    size_t len = 0;
    out[len++] = (uint8_t)(message_len + 1U);
    memset(&out[len], 0x01, message_len);
    len += message_len;
    out[len++] = 0x00;
    return len;
}

ZTEST(stow_cobs_transport, test_oversized_frame_is_dropped_and_the_next_frame_decodes)
{
    uint8_t stream[2U * sizeof(request_frame) + LINK_MESSAGE_SIZE + 3U];
    size_t len = 0;
    memcpy(&stream[len], request_frame, sizeof(request_frame));
    len += sizeof(request_frame);
    len += build_oversized_frame(&stream[len]);
    memcpy(&stream[len], request_frame, sizeof(request_frame));
    len += sizeof(request_frame);

    zassert_equal(stow_cobs_transport_open(&link), 0);
    zassert_equal(stow_cobs_transport_feed(&link, stream, len), 0);
    expect_frame(response_frame, sizeof(response_frame));
    expect_frame(response_frame, sizeof(response_frame));
    test_sync();
    struct net_buf* frame = NULL;
    zassert_not_equal(k_msgq_get(&link_frame_msgq, &frame, K_MSEC(50)), 0, "the oversized frame was answered");
}

ZTEST(stow_cobs_transport, test_oversized_frame_split_across_feeds_resyncs)
{
    uint8_t frame[LINK_MESSAGE_SIZE + 3U];
    size_t len = build_oversized_frame(frame);

    zassert_equal(stow_cobs_transport_open(&link), 0);
    zassert_equal(stow_cobs_transport_feed(&link, frame, len - 1U), 0);
    zassert_equal(stow_cobs_transport_feed(&link, &frame[len - 1U], 1U), 0);
    zassert_equal(stow_cobs_transport_feed(&link, request_frame, sizeof(request_frame)), 0);
    expect_frame(response_frame, sizeof(response_frame));
}
