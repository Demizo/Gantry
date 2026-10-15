/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "test_common.h"

#include <gantry/error.h>
#include <string.h>

NET_BUF_POOL_DEFINE(test_link_pool, 48, TEST_BUF_SIZE, 0, NULL);

struct held_message
{
    struct test_link* link;
    struct net_buf* buf;
};

K_MSGQ_DEFINE(test_held, sizeof(struct held_message), 16, sizeof(void*));
K_MSGQ_DEFINE(test_hooks, sizeof(struct test_hook_event), 32, sizeof(void*));
K_MSGQ_DEFINE(test_updates, sizeof(event_t*), 8, sizeof(void*));

static bool started;

STOW_CLIENT_ENDPOINT_DEFINE(test_peer_client, &peer_stow_client_schema, 0);
STOW_CLIENT_ENDPOINT_DEFINE(test_variant_client, &variant_stow_client_schema, 0);
static atomic_t block_disconnected;
K_SEM_DEFINE(disconnected_entered, 0, 1);
K_SEM_DEFINE(disconnected_gate, 0, 1);
size_t test_server_headroom;
size_t test_client_headroom;
static uint32_t next_session_id = 100;

static void record_hook(struct stow_client_peer* peer, enum test_hook hook, int err)
{
    struct test_hook_event event = { .peer = peer, .hook = hook, .err = err };
    zassert_equal(k_msgq_put(&test_hooks, &event, K_NO_WAIT), 0, "hook log full");
}

static void on_connected(struct stow_client_peer* peer, int err)
{
    struct stow_client_peer* held = stow_client_peer_ref(peer);
    record_hook(held, TEST_HOOK_CONNECTED, err);
    PASS_OWNERSHIP(held);
}

static void purge_hooks(void)
{
    struct test_hook_event event;
    while (k_msgq_get(&test_hooks, &event, K_NO_WAIT) == 0)
    {
        struct stow_client_peer* peer = event.peer;
        if (event.hook == TEST_HOOK_CONNECTED)
        {
            stow_client_peer_unref(&peer);
        }
    }
}

static void on_disconnected(struct stow_client_peer* peer)
{
    record_hook(peer, TEST_HOOK_DISCONNECTED, 0);
    if (atomic_cas(&block_disconnected, 1, 0))
    {
        k_sem_give(&disconnected_entered);
        (void)k_sem_take(&disconnected_gate, K_FOREVER);
    }
}

void test_block_disconnected(void)
{
    k_sem_reset(&disconnected_entered);
    k_sem_reset(&disconnected_gate);
    atomic_set(&block_disconnected, 1);
}

void test_await_blocked_disconnected(void)
{
    zassert_equal(k_sem_take(&disconnected_entered, TEST_WAIT), 0, "disconnected hook did not run");
}

void test_unblock_disconnected(void) { k_sem_give(&disconnected_gate); }

static void on_update(event_t* event)
{
    event_ref(event);
    if (k_msgq_put(&test_updates, &event, K_NO_WAIT) != 0)
    {
        event_unref(&event);
        zassert_unreachable("update log full");
    }
    PASS_OWNERSHIP(event);
}

static void purge_updates(void)
{
    event_t* event = NULL;
    while (k_msgq_get(&test_updates, &event, K_NO_WAIT) == 0)
    {
        event_unref(&event);
    }
}

STOW_CLIENT_CB_DEFINE(test_hooks_cb) = {
    .connected = on_connected,
    .disconnected = on_disconnected,
    .update = on_update,
};

static bool take_one(atomic_t* counter)
{
    atomic_val_t value = atomic_get(counter);
    while (value > 0)
    {
        if (atomic_cas(counter, value, value - 1))
        {
            return true;
        }
        value = atomic_get(counter);
    }
    return false;
}

static int send_to_server(struct stow_transport* transport, uint32_t session_id, struct net_buf* buf)
{
    struct test_link* link = CONTAINER_OF(transport, struct test_link, to_server);
    if (take_one(&link->refuse_requests))
    {
        return -ENOBUFS;
    }
    if (!take_one(&link->drop_requests))
    {
        stow_protocol_endpoint.receive(&stow_protocol_endpoint, session_id, buf);
    }
    net_buf_unref(buf);
    return SUCCESS;
}

static int send_to_client(struct stow_transport* transport, uint32_t session_id, struct net_buf* buf)
{
    struct test_link* link = CONTAINER_OF(transport, struct test_link, to_client);
    if (take_one(&link->drop_responses))
    {
        net_buf_unref(buf);
        return SUCCESS;
    }
    if (take_one(&link->hold_responses))
    {
        struct held_message held = { .link = link, .buf = buf };
        return (k_msgq_put(&test_held, &held, K_NO_WAIT) == 0) ? SUCCESS : -ENOBUFS;
    }

    link->client->endpoint.receive(&link->client->endpoint, session_id, buf);
    if (take_one(&link->duplicate_responses))
    {
        struct net_buf* copy_buf = net_buf_ref(buf);
        struct held_message copy = { .link = link, .buf = copy_buf };
        zassert_equal(k_msgq_put(&test_held, &copy, K_NO_WAIT), 0);
        PASS_OWNERSHIP(copy_buf);
    }
    net_buf_unref(buf);
    return SUCCESS;
}

void test_reset(void)
{
    if (!started)
    {
        stow_init();
        zassert_equal(stow_protocol_init(), 0);
        started = true;
    }
    stow_init();
    test_release_held();
    purge_hooks();
    purge_updates();
    test_server_headroom = 0;
    test_client_headroom = 0;
}

struct stow_client_peer* test_connect(
    struct test_link* link, const struct stow_client_endpoint* client, stow_role_t roles)
{
    test_link_init(link, client);
    zassert_equal(
        stow_protocol_endpoint.session_opened(&stow_protocol_endpoint, link->session_id, roles, &link->to_client), 0);
    zassert_equal(test_open_client(link), 0);

    struct test_hook_event event = test_next_hook(TEST_WAIT);
    zassert_equal(event.hook, TEST_HOOK_CONNECTED);
    zassert_equal(event.err, 0, "peer was not verified (%d)", event.err);
    zassert_equal(stow_client_peer_session_id(event.peer), link->session_id);
    zassert_equal_ptr(stow_client_peer_endpoint(event.peer), client);
    struct stow_client_peer* peer = event.peer;
    PASS_OWNERSHIP(peer);
    return peer;
}

int test_open_client(struct test_link* link)
{
    return link->client->endpoint.session_opened(&link->client->endpoint, link->session_id, 0, &link->to_server);
}

void test_link_init(struct test_link* link, const struct stow_client_endpoint* client)
{
    memset(link, 0, sizeof(*link));
    link->client = client;
    link->session_id = next_session_id++;
    link->to_server = (struct stow_transport){
        .send = send_to_server,
        .pool = &test_link_pool,
        .headroom = test_client_headroom,
    };
    link->to_client = (struct stow_transport){
        .send = send_to_client,
        .pool = &test_link_pool,
        .headroom = test_server_headroom,
    };
}

void test_settle(void)
{
    k_tid_t self = k_current_get();
    int priority = k_thread_priority_get(self);
    k_thread_priority_set(self, MAX(CONFIG_STOW_CLIENT_THREAD_PRIORITY, CONFIG_STOW_PROTOCOL_THREAD_PRIORITY) + 1);
    k_thread_priority_set(self, priority);
}

void test_close_client(struct test_link* link)
{
    link->client->endpoint.session_closed(&link->client->endpoint, link->session_id);
}

void test_disconnect(struct test_link* link, struct stow_client_peer** peer)
{
    stow_protocol_endpoint.session_closed(&stow_protocol_endpoint, link->session_id);
    test_close_client(link);
    test_release_held();
    test_settle();
    stow_client_peer_unref(peer);
    purge_hooks();
}

void test_release_held(void)
{
    struct held_message held;
    while (k_msgq_get(&test_held, &held, K_NO_WAIT) == 0)
    {
        struct net_buf* buf = held.buf;
        held.link->client->endpoint.receive(&held.link->client->endpoint, held.link->session_id, buf);
        net_buf_unref(buf);
    }
}

void test_await_held(uint32_t count)
{
    for (int i = 0; i < 200 && k_msgq_num_used_get(&test_held) < count; i++)
    {
        k_msleep(1);
    }
    zassert_equal(k_msgq_num_used_get(&test_held), count, "responses were not held");
}

int test_next_update(k_timeout_t timeout, event_t** event_out) { return k_msgq_get(&test_updates, event_out, timeout); }

struct test_hook_event test_next_hook(k_timeout_t timeout)
{
    struct test_hook_event event = { 0 };
    if (k_msgq_get(&test_hooks, &event, timeout) != 0)
    {
        return (struct test_hook_event){ .hook = TEST_HOOK_NONE };
    }
    return event;
}

void test_async_init(struct test_async* async)
{
    memset(async, 0, sizeof(*async));
    k_sem_init(&async->done, 0, 1);
}

void test_capture_cb(event_t* event, void* user_data)
{
    struct test_async* async = user_data;
    const struct stow_client_result_payload* result = (const struct stow_client_result_payload*)event->data.buf;
    async->err = result->err;
    async->index = result->index;
    async->count = result->count;
    if (result->count > 0U)
    {
        event_ref(event);
        async->event = event;
        PASS_OWNERSHIP(event);
    }
    k_sem_give(&async->done);
}

const struct stow_client_value* test_async_values(const struct test_async* async)
{
    return ((const struct stow_client_result_payload*)async->event->data.buf)->values;
}

void test_async_release(struct test_async* async)
{
    event_t* event = async->event;
    async->event = NULL;
    event_unref(&event);
}

uint32_t test_blocks_in_use(void)
{
    uint32_t total_used = 0;
    for (uint8_t pool = 0; pool < CONFIG_MEM_POOL_COUNT; pool++)
    {
        uint32_t used = 0;
        uint32_t total = 0;
        (void)mem_get_pool_usage(pool, &used, &total);
        total_used += used;
    }
    return total_used;
}
