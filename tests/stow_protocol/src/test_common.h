/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <gantry/memory.h>
#include <gantry/stow/stow.h>
#include <gantry/stow/stow_protocol.h>
#include <gantry/stow/stow_serde.h>
#include <gantry/stow/stow_transport.h>
#include <generated_stow_items.h>
#include <stdint.h>
#include <string.h>
#include <zcbor_decode.h>
#include <zcbor_encode.h>
#include <zephyr/kernel.h>
#include <zephyr/net_buf.h>
#include <zephyr/ztest.h>
#include <zephyr/ztest_assert.h>

/**
 * @brief Sessions available to tests; one slot is reserved for @ref test_sync
 */
#define TEST_FREE_SESSIONS (CONFIG_STOW_PROTOCOL_MAX_SESSIONS - 1)

/**
 * @brief Size of each buffer in the shared test pools
 */
#define TEST_BUF_DATA_SIZE 320

/**
 * @brief Tag used by requests that do not exercise tagging
 */
#define TEST_TAG 17U

/**
 * @brief Maximum number of TX responses buffered during a single test
 */
#define TEST_MAX_PENDING_TX 32

/**
 * @brief Number of buffers in the fake transport's pool
 */
#define TEST_TX_BUF_COUNT 32

/**
 * @brief Reserved headroom for the test TX pool (verifies app head usage)
 */
#define TEST_TX_HEADROOM 8

/**
 * @brief Reserved tailroom for the test TX pool (verifies app tail usage)
 */
#define TEST_TX_TAILROOM 4

/**
 * @brief A captured TX response (paired session_id and net_buf)
 */
struct test_capture
{
    uint32_t session_id;
    struct net_buf* buf;
};

/**
 * @brief Fake transport that captures every response
 */
extern struct stow_transport test_transport;

/**
 * @brief Make the fake transport refuse sends
 *
 * @param refuse Whether sends are refused
 */
void test_refuse_sends(bool refuse);

/**
 * @brief Open a session through the protocol endpoint
 *
 * @param session_id Session to open
 * @param roles Roles granted to the session
 * @param transport Transport that carries the session's messages
 *
 * @return The endpoint's result
 */
int test_endpoint_open(uint32_t session_id, stow_role_t roles, struct stow_transport* transport);

/**
 * @brief Close a session through the protocol endpoint
 *
 * @param session_id Session to close
 */
void test_endpoint_close(uint32_t session_id);

/**
 * @brief Make the fake transport block inside send until unblocked
 *
 * @param block Whether sends block
 */
void test_block_sends(bool block);

/**
 * @brief Wait until the protocol thread is blocked inside send
 */
void test_await_blocked_send(void);

/**
 * @brief Open a session on the fake transport
 *
 * @param session_id Session to open
 * @param roles Roles granted to the session
 */
void test_open_session(uint32_t session_id, stow_role_t roles);

/**
 * @brief Allocate an RX net_buf from the shared test pool
 *
 * @details The caller writes CBOR bytes into the buf using net_buf_add and
 * passes it to the protocol endpoint. The protocol takes its own
 * reference; the caller should net_buf_unref after submitting.
 */
struct net_buf* test_alloc_rx(void);

/**
 * @brief Submit a hand-crafted CBOR request to the protocol
 *
 * @param session_id The session that owns the request
 * @param data Encoded CBOR bytes
 * @param len Length of the encoded payload
 *
 * @return 0 when the request was handed to the endpoint, -ENOMEM when no RX buffer was free
 */
int test_submit_rx(uint32_t session_id, const uint8_t* data, size_t len);

/**
 * @brief Wait for and return the next captured TX response
 *
 * @return Captured response, or {0, NULL} on timeout
 */
struct test_capture test_await_tx(k_timeout_t timeout);

/**
 * @brief Drain any pending captures, releasing their buffers
 */
void test_drain_captures(void);

/**
 * @brief Force the protocol thread to flush pending events
 *
 * @details Sends a Hello request on a dedicated session and waits for its
 * response below the protocol thread's priority, so the thread is idle on return.
 */
void test_sync(void);

/**
 * @brief Let the protocol thread run until it has nothing left to do, without sending it anything
 */
void test_settle(void);

/**
 * @brief Decode the outer array header, message code and request tag of a captured response
 *
 * @param buf Captured response net_buf
 * @param[out] out_cmd Populated with the decoded message code
 * @param[out] out_tag Populated with the echoed tag, or @ref STOW_SERDE_ABSENT for null; may be NULL
 * @param[out] dec Initialized decoder, positioned after the header.
 *                 Caller is responsible for finishing the list decode.
 * @param dec_states Number of states in @p dec
 *
 * @return true on success
 */
bool test_decode_response_header(
    struct net_buf* buf, uint32_t* out_cmd, uint32_t* out_tag, zcbor_state_t* dec, size_t dec_states);

/**
 * @brief Decode a captured Error response
 *
 * @param buf Captured response net_buf
 * @param[out] out_error Populated with the decoded error
 *
 * @return true when @p buf holds a well-formed Error
 */
bool test_decode_error(struct net_buf* buf, struct stow_serde_error* out_error);

/**
 * @brief Encode a request with no arguments [cmd, tag] into out_buf
 *
 * @return Encoded length on success, negative errno on failure
 */
int test_build_simple(uint8_t* out_buf, size_t out_buf_size, uint32_t cmd, uint32_t tag);

/**
 * @brief Encode a request with one item [cmd, tag, id]
 */
int test_build_with_id(uint8_t* out_buf, size_t out_buf_size, uint32_t cmd, uint32_t tag, uint32_t item_id);

/**
 * @brief Encode a Set request for an int item
 */
int test_build_set_int(uint8_t* out_buf, size_t out_buf_size, uint32_t tag, uint32_t item_id, int32_t value);

/**
 * @brief Encode a Set request for a string item
 */
int test_build_set_string(uint8_t* out_buf, size_t out_buf_size, uint32_t tag, uint32_t item_id, const char* value);

/**
 * @brief Encode a request with a list of items [cmd, tag, id, ...]
 */
int test_build_list(
    uint8_t* out_buf, size_t out_buf_size, uint32_t cmd, uint32_t tag, const uint32_t* ids, uint8_t count);

/**
 * @brief Encode a Set request with two int items
 */
int test_build_set_ints(
    uint8_t* out_buf, size_t out_buf_size, uint32_t tag, uint32_t id1, int32_t val1, uint32_t id2, int32_t val2);

/**
 * @brief Reset shared test state between tests
 */
void test_reset(void);
