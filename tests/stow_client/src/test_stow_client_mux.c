/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gantry/error.h>
#include <gantry/stow/stow_cobs_transport.h>
#include <gantry/stow/stow_wire.h>

#include "test_common.h"

NET_BUF_POOL_DEFINE(mux_message_pool, 24, TEST_BUF_SIZE, 0, NULL);
NET_BUF_POOL_DEFINE(mux_frame_pool, 24, TEST_BUF_SIZE + TEST_BUF_SIZE / 2, 0, NULL);

/**
 * @brief One end of a COBS link whose frames are fed straight into the other end
 */
struct mux_end
{
    struct stow_cobs_transport cobs;
    struct mux_end* other;
    struct k_mutex feeding;
    atomic_t written;
};

static struct mux_end left;
static struct mux_end right;

static int write_frame(struct net_buf* frame, void* user_data)
{
    struct mux_end* end = user_data;
    struct mux_end* other = end->other;
    atomic_inc(&end->written);
    struct k_mutex* feeding = &other->feeding;
    k_mutex_lock(feeding, K_FOREVER);
    int ret = stow_cobs_transport_feed(&other->cobs, frame->data, frame->len);
    k_mutex_unlock(feeding);
    if (ret != SUCCESS)
    {
        return ret;
    }
    net_buf_unref(frame);
    return SUCCESS;
}

static void end_init(struct mux_end* end, struct mux_end* other, uint32_t session_id, bool server)
{
    end->other = other;
    k_mutex_init(&end->feeding);
    atomic_clear(&end->written);
    const struct stow_cobs_transport_config config = {
        .server = server ? &stow_protocol_endpoint : NULL,
        .session_id = session_id,
        .roles = STOW_ROLE_GUEST,
        .message_pool = &mux_message_pool,
        .frame_pool = &mux_frame_pool,
        .write = write_frame,
        .user_data = end,
    };
    zassert_equal(stow_cobs_transport_init(&end->cobs, &config), 0);
}

static struct stow_client_peer* open_client(struct mux_end* end, int expected)
{
    zassert_equal(stow_cobs_transport_open_client(&end->cobs, &test_peer_client.endpoint), 0);
    struct test_hook_event event = test_next_hook(TEST_WAIT);
    zassert_equal(event.hook, TEST_HOOK_CONNECTED);
    zassert_equal(event.err, expected, "connected with %d", event.err);
    struct stow_client_peer* peer = event.peer;
    PASS_OWNERSHIP(peer);
    return peer;
}

static void close_ends(void)
{
    stow_cobs_transport_close(&left.cobs);
    stow_cobs_transport_close(&right.cobs);
    test_settle();
}

static void before_each(void* fixture)
{
    ARG_UNUSED(fixture);
    test_reset();
}

ZTEST_SUITE(stow_client_mux, NULL, NULL, before_each, NULL, NULL);

ZTEST(stow_client_mux, test_each_end_is_server_and_client_over_one_link)
{
    end_init(&left, &right, 500, true);
    end_init(&right, &left, 501, true);
    zassert_equal(stow_cobs_transport_open(&left.cobs), 0);
    zassert_equal(stow_cobs_transport_open(&right.cobs), 0);
    zassert_equal(stow_set(STOW_ID_COUNTER, (data_value_t){ .type = STOW_ITEM_TYPE_INT, .data.int_value = 21 }), 0);

    struct stow_client_peer* from_left = open_client(&left, 0);
    struct stow_client_peer* from_right = open_client(&right, 0);

    struct stow_client_value value;
    zassert_equal(stow_client_get_one(from_left, PEER_ID_COUNTER, &value), 0);
    zassert_equal(value.value.data.int_value, 21);
    stow_client_release(&test_peer_client, &value);
    zassert_equal(stow_client_get_one(from_right, PEER_ID_COUNTER, &value), 0);
    zassert_equal(value.value.data.int_value, 21);
    stow_client_release(&test_peer_client, &value);

    stow_cobs_transport_close_client(&left.cobs);
    zassert_equal(test_next_hook(TEST_WAIT).hook, TEST_HOOK_DISCONNECTED);
    zassert_equal(stow_client_get_one(from_right, PEER_ID_COUNTER, &value), 0, "server session closed too");
    stow_client_release(&test_peer_client, &value);

    close_ends();
    stow_client_peer_unref(&from_left);
    stow_client_peer_unref(&from_right);
}

ZTEST(stow_client_mux, test_a_request_with_no_server_is_answered_at_once)
{
    end_init(&left, &right, 502, false);
    end_init(&right, &left, 503, false);
    zassert_equal(stow_cobs_transport_open(&left.cobs), 0);
    zassert_equal(stow_cobs_transport_open(&right.cobs), 0);

    int64_t start = k_uptime_get();
    struct stow_client_peer* peer = open_client(&left, -EPROTO);
    zassert_true(k_uptime_get() - start < CONFIG_STOW_CLIENT_TIMEOUT_MS / 2, "waited for a timeout");

    close_ends();
    stow_client_peer_unref(&peer);
}

ZTEST(stow_client_mux, test_a_client_session_needs_an_open_link)
{
    end_init(&left, &right, 506, true);
    end_init(&right, &left, 507, true);
    zassert_equal(stow_cobs_transport_open_client(&left.cobs, &test_peer_client.endpoint), -ENOTCONN);
    zassert_equal(stow_cobs_transport_open(&left.cobs), 0);
    stow_cobs_transport_close(&left.cobs);
    zassert_equal(stow_cobs_transport_open_client(&left.cobs, &test_peer_client.endpoint), -ENOTCONN);
    test_settle();
}

ZTEST(stow_client_mux, test_a_response_with_no_client_session_is_dropped)
{
    end_init(&left, &right, 504, true);
    end_init(&right, &left, 505, true);
    zassert_equal(stow_cobs_transport_open(&left.cobs), 0);

    static const uint8_t ok_frame[] = { 0x04, 0x82, STOW_MSG_OK, 0x05, 0x00 };
    static const uint8_t bad_tag_frame[] = { 0x05, 0x82, STOW_MSG_OK, 0x18, STOW_TAG_MAX + 7U, 0x00 };
    zassert_equal(stow_cobs_transport_feed(&left.cobs, ok_frame, sizeof(ok_frame)), 0);
    zassert_equal(stow_cobs_transport_feed(&left.cobs, bad_tag_frame, sizeof(bad_tag_frame)), 0);
    test_settle();
    zassert_equal(atomic_get(&left.written), 0, "the server answered a response");

    close_ends();
}
