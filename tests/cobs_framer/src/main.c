/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gantry/cobs_framer.h>
#include <string.h>
#include <zephyr/net_buf.h>
#include <zephyr/ztest.h>

#define FRAME_BUF_SIZE 8

NET_BUF_POOL_DEFINE(single_pool, 1, FRAME_BUF_SIZE, 0, NULL);
NET_BUF_POOL_DEFINE(frame_pool, 4, FRAME_BUF_SIZE, 0, NULL);
NET_BUF_POOL_DEFINE(encode_pool, 1, COBS_ENCODE_MAX_SIZE(FRAME_BUF_SIZE), 0, NULL);

static struct cobs_frame_decoder decoder;
static struct net_buf* frames[4];
static size_t frame_count;
static bool held_by_decoder;

static void on_frame(struct net_buf* buf, void* user_data)
{
    ARG_UNUSED(user_data);
    held_by_decoder = held_by_decoder || (decoder.rx_buf == buf);
    zassert_true(frame_count < ARRAY_SIZE(frames));
    frames[frame_count++] = buf;
}

static void release_frames(void)
{
    while (frame_count > 0)
    {
        net_buf_unref(frames[--frame_count]);
    }
}

static void expect_frame(size_t index, const uint8_t* payload, size_t len)
{
    zassert_true(index < frame_count, "frame %u missing", index);
    zassert_equal(frames[index]->len, len);
    zassert_mem_equal(frames[index]->data, payload, len);
}

static void before_each(void* fixture)
{
    ARG_UNUSED(fixture);
    frame_count = 0;
    held_by_decoder = false;
}

static void after_each(void* fixture)
{
    ARG_UNUSED(fixture);
    release_frames();
    if (decoder.rx_buf != NULL)
    {
        net_buf_unref(decoder.rx_buf);
        decoder.rx_buf = NULL;
    }
}

ZTEST_SUITE(cobs_framer, NULL, NULL, before_each, after_each, NULL);

ZTEST(cobs_framer, test_callback_gets_a_frame_the_decoder_no_longer_holds)
{
    zassert_equal(cobs_frame_decoder_init(&decoder, &frame_pool, on_frame, NULL), 0);
    const uint8_t stream[] = { 0x03, 0x11, 0x22, 0x00 };
    zassert_equal(cobs_frame_decoder_feed(&decoder, stream, sizeof(stream)), 0);

    const uint8_t payload[] = { 0x11, 0x22 };
    expect_frame(0, payload, sizeof(payload));
    zassert_false(held_by_decoder, "the decoder still held the frame it handed over");
}

ZTEST(cobs_framer, test_feed_resyncs_after_running_out_of_buffers)
{
    zassert_equal(cobs_frame_decoder_init(&decoder, &single_pool, on_frame, NULL), 0);
    const uint8_t first[] = { 0x03, 0x11, 0x22, 0x00, 0x03, 0x33 };
    zassert_equal(cobs_frame_decoder_feed(&decoder, first, sizeof(first)), -ENOMEM);
    zassert_equal(frame_count, 1);
    release_frames();

    const uint8_t rest[] = { 0x44, 0x00, 0x02, 0x55, 0x00 };
    zassert_equal(cobs_frame_decoder_feed(&decoder, rest, sizeof(rest)), 0);
    const uint8_t payload[] = { 0x55 };
    zassert_equal(frame_count, 1, "the frame cut short by the missing buffer was delivered");
    expect_frame(0, payload, sizeof(payload));
}

ZTEST(cobs_framer, test_oversized_frame_is_dropped_whole)
{
    zassert_equal(cobs_frame_decoder_init(&decoder, &frame_pool, on_frame, NULL), 0);
    uint8_t stream[FRAME_BUF_SIZE + 6];
    size_t len = 0;
    stream[len++] = FRAME_BUF_SIZE + 2;
    for (size_t i = 0; i < FRAME_BUF_SIZE + 1; i++)
    {
        stream[len++] = (uint8_t)(i + 1);
    }
    stream[len++] = 0x00;
    stream[len++] = 0x02;
    stream[len++] = 0x55;
    stream[len++] = 0x00;
    zassert_equal(cobs_frame_decoder_feed(&decoder, stream, len), 0);

    const uint8_t payload[] = { 0x55 };
    zassert_equal(frame_count, 1, "part of the oversized frame was delivered");
    expect_frame(0, payload, sizeof(payload));
}

ZTEST(cobs_framer, test_encode_leaves_its_input_intact)
{
    struct net_buf* input = net_buf_alloc(&frame_pool, K_NO_WAIT);
    zassert_not_null(input);
    const uint8_t payload[] = { 0x11, 0x00, 0x22 };
    net_buf_add_mem(input, payload, sizeof(payload));

    struct net_buf* output = NULL;
    zassert_equal(cobs_frame_encode(&encode_pool, input, &output), 0);
    zassert_equal(input->len, sizeof(payload));
    zassert_mem_equal(input->data, payload, sizeof(payload));

    const uint8_t encoded[] = { 0x02, 0x11, 0x02, 0x22, 0x00 };
    zassert_equal(output->len, sizeof(encoded));
    zassert_mem_equal(output->data, encoded, sizeof(encoded));
    net_buf_unref(output);
    net_buf_unref(input);
}
