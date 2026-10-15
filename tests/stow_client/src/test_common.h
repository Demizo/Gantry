/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <gantry/memory.h>
#include <gantry/stow/stow.h>
#include <gantry/stow/stow_client.h>
#include <gantry/stow/stow_protocol.h>
#include <gantry/stow/stow_transport.h>
#include <generated_peer_stow_client.h>
#include <generated_stow_enums.h>
#include <generated_stow_items.h>
#include <generated_variant_stow_client.h>
#include <stdint.h>
#include <zephyr/kernel.h>
#include <zephyr/net_buf.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/ztest.h>

/**
 * @brief Size of each buffer the links carry
 */
#define TEST_BUF_SIZE 256

/**
 * @brief How long tests wait for something the client should do promptly
 */
#define TEST_WAIT K_SECONDS(2)

/**
 * @brief Lifecycle hooks the tests record
 */
enum test_hook
{
    TEST_HOOK_NONE,
    TEST_HOOK_CONNECTED,
    TEST_HOOK_DISCONNECTED,
};

/**
 * @brief One recorded lifecycle hook
 */
struct test_hook_event
{
    struct stow_client_peer* peer;
    enum test_hook hook;
    int err;
};

/**
 * @brief A loopback link between the client and the server, with fault injection
 *
 * @details Each counter applies to that many of the next messages in its direction. A duplicated response is
 * delivered at once and its copy is held.
 */
struct test_link
{
    const struct stow_client_endpoint* client;
    struct stow_transport to_server;
    struct stow_transport to_client;
    uint32_t session_id;
    atomic_t drop_requests;
    atomic_t refuse_requests;
    atomic_t drop_responses;
    atomic_t duplicate_responses;
    atomic_t hold_responses;
};

/**
 * @brief The outcome of an asynchronous request, captured by @ref test_capture_cb
 */
struct test_async
{
    struct k_sem done;
    int err;
    uint16_t index;
    uint16_t count;
    event_t* event;
};

/**
 * @brief Start the Stow, the server and the client once, and clear test state
 */
void test_reset(void);

/**
 * @brief Client endpoint for peers bound with the test schema
 */
extern const struct stow_client_endpoint test_peer_client;

/**
 * @brief Client endpoint for peers bound with the variant schema
 */
extern const struct stow_client_endpoint test_variant_client;

/**
 * @brief Open a new link on a client endpoint and wait for its connected hook
 *
 * @param link Link to set up
 * @param client Client endpoint the session opens on
 * @param roles Roles the server grants the session
 *
 * @return A reference to the connected peer
 * @acquires stow_client_peer return
 */
struct stow_client_peer* test_connect(
    struct test_link* link, const struct stow_client_endpoint* client, stow_role_t roles);

/**
 * @brief Close both ends of a link and release the peer
 *
 * @param link The link
 * @param peer The reference to release
 * @releases stow_client_peer peer
 */
void test_disconnect(struct test_link* link, struct stow_client_peer** peer);

/**
 * @brief Let the client and server threads run until they have nothing left to do
 */
void test_settle(void);

/**
 * @brief Close the link's session on the client side only
 *
 * @param link The link
 */
void test_close_client(struct test_link* link);

/**
 * @brief Deliver every held response
 */
void test_release_held(void);

/**
 * @brief Wait until a number of responses are held
 *
 * @param count Number of held responses to wait for
 */
void test_await_held(uint32_t count);

/**
 * @brief Make the next disconnected hook block the client thread until @ref test_unblock_disconnected
 */
void test_block_disconnected(void);

/**
 * @brief Wait until the client thread is blocked in the disconnected hook
 */
void test_await_blocked_disconnected(void);

/**
 * @brief Let the client thread leave the disconnected hook
 */
void test_unblock_disconnected(void);

/**
 * @brief Prepare a link without connecting it
 *
 * @param link Link to set up
 * @param client Client endpoint the link's sessions open on
 */
void test_link_init(struct test_link* link, const struct stow_client_endpoint* client);

/**
 * @brief Open the client end of a link
 *
 * @param link The link
 *
 * @return The endpoint's result
 */
int test_open_client(struct test_link* link);

/**
 * @brief Headroom the server's side of new links reserves, which shrinks the largest message the server reports
 */
extern size_t test_server_headroom;

/**
 * @brief Headroom the client's side of new links reserves, which shrinks the requests the client can build
 */
extern size_t test_client_headroom;

/**
 * @brief Wait for the next Update event the hooks kept
 *
 * @param timeout How long to wait
 * @param[out] event_out The event, for the caller to release
 *
 * @return 0 when an Update arrived, otherwise a negative errno
 * @acquires memory event_out on-success
 */
int test_next_update(k_timeout_t timeout, event_t** event_out);

/**
 * @brief Wait for the next recorded lifecycle hook
 *
 * @details A connected hook's event holds a reference to its peer, which the caller releases.
 *
 * @param timeout How long to wait
 *
 * @return The hook, or one with @ref TEST_HOOK_NONE on timeout
 */
struct test_hook_event test_next_hook(k_timeout_t timeout);

/**
 * @brief Callback that records an asynchronous outcome into a @ref test_async, keeping the event when it has values
 */
void test_capture_cb(event_t* event, void* user_data);

/**
 * @brief Prepare a @ref test_async for a request
 *
 * @param async The capture
 */
void test_async_init(struct test_async* async);

/**
 * @brief The values of a captured Get
 *
 * @param async The capture
 *
 * @return The values, valid until @ref test_async_release
 */
const struct stow_client_value* test_async_values(const struct test_async* async);

/**
 * @brief Release the result event a @ref test_async kept
 *
 * @param async The capture
 */
void test_async_release(struct test_async* async);

/**
 * @brief Blocks in use across every memory pool
 *
 * @return Number of blocks allocated
 */
uint32_t test_blocks_in_use(void);
