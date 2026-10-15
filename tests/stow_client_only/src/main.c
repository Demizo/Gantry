/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gantry/error.h>
#include <gantry/stow/stow_client.h>
#include <gantry/stow/stow_cobs_transport.h>
#include <generated_device_stow_client.h>
#include <zephyr/kernel.h>
#include <zephyr/net_buf.h>
#include <zephyr/ztest.h>

#define SESSION_ID 7

NET_BUF_POOL_DEFINE(message_pool, 4, 128, 0, NULL);
NET_BUF_POOL_DEFINE(frame_pool, 4, 160, 0, NULL);
K_MSGQ_DEFINE(frames, sizeof(struct net_buf*), 4, sizeof(void*));
K_MSGQ_DEFINE(verifications, sizeof(int), 4, sizeof(int));

STOW_CLIENT_ENDPOINT_DEFINE(device_client, &device_stow_client_schema, 0);

static struct stow_client_peer* device;

static int write_frame(struct net_buf* frame, void* user_data)
{
    ARG_UNUSED(user_data);
    return (k_msgq_put(&frames, &frame, K_NO_WAIT) == 0) ? SUCCESS : -ENOBUFS;
}

static void on_connected(struct stow_client_peer* peer, int err)
{
    device = stow_client_peer_ref(peer);
    (void)k_msgq_put(&verifications, &err, K_NO_WAIT);
}

STOW_CLIENT_CB_DEFINE(client_only_hooks) = {
    .connected = on_connected,
};

ZTEST_SUITE(stow_client_only, NULL, NULL, NULL, NULL, NULL);

ZTEST(stow_client_only, test_a_client_without_a_local_stow_greets_its_peer)
{
    static struct stow_cobs_transport link;
    const struct stow_cobs_transport_config config = {
        .endpoint = &device_client.endpoint,
        .session_id = SESSION_ID,
        .message_pool = &message_pool,
        .frame_pool = &frame_pool,
        .write = write_frame,
    };
    zassert_equal(stow_cobs_transport_init(&link, &config), 0);
    zassert_equal(stow_cobs_transport_open(&link), 0);

    struct net_buf* frame = NULL;
    zassert_equal(k_msgq_get(&frames, &frame, K_SECONDS(1)), 0, "no Hello");
    static const uint8_t hello[] = { 0x02, 0x82, 0x01, 0x01, 0x00 };
    zassert_equal(frame->len, sizeof(hello));
    zassert_mem_equal(frame->data, hello, sizeof(hello));
    net_buf_unref(frame);

    stow_cobs_transport_close(&link);
    int err = 0;
    zassert_equal(k_msgq_get(&verifications, &err, K_SECONDS(1)), 0);
    zassert_equal(err, -ENOTCONN);
    zassert_equal(stow_client_peer_session_id(device), SESSION_ID);

    device_Point_t point = { .x = 1 };
    data_value_t value = { .type = STOW_ITEM_TYPE_STRUCT, .data.raw_value = &point };
    zassert_equal(stow_client_set_one(device, DEVICE_ID_POSITION, value), -ENOTCONN);
    stow_client_peer_unref(&device);
    zassert_is_null(device);
}
