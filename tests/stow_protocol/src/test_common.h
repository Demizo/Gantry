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
 * @brief Maximum number of TX responses buffered during a single test
 */
#define TEST_MAX_PENDING_TX 16

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
 * @brief Allocate an RX net_buf from the shared test pool
 *
 * @details The caller writes CBOR bytes into the buf using net_buf_add and
 * passes it to stow_protocol_handle_rx. The protocol takes its own
 * reference; the caller should net_buf_unref after submitting.
 */
struct net_buf* test_alloc_rx(void);

/**
 * @brief Submit a hand-crafted CBOR request to the protocol
 *
 * @param session_id The session that owns the request
 * @param roles Role bitmask reported with the request
 * @param data Encoded CBOR bytes
 * @param len Length of the encoded payload
 */
int test_submit_rx(uint32_t session_id, stow_role_t roles, const uint8_t* data, size_t len);

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
 * @details Sends a Version request to a dedicated synchronization session
 * and waits for the response. By the time we receive it, the worker
 * thread has drained every event queued before our request.
 */
void test_sync(void);

/**
 * @brief Decode the outer array header and command tag of a captured response
 *
 * @param buf Captured response net_buf
 * @param[out] out_cmd Populated with the decoded command tag
 * @param[out] dec Initialized decoder, positioned right after the command tag.
 *                 Caller is responsible for finishing the list decode.
 *
 * @return true on success
 */
bool test_decode_response_header(struct net_buf* buf, uint32_t* out_cmd, zcbor_state_t* dec, size_t dec_states);

/**
 * @brief Encode a single-tag request [cmd] into out_buf
 *
 * @return Encoded length on success, negative errno on failure
 */
int test_build_simple(uint8_t* out_buf, size_t out_buf_size, uint32_t cmd);

/**
 * @brief Encode a two-element request [cmd, id]
 */
int test_build_with_id(uint8_t* out_buf, size_t out_buf_size, uint32_t cmd, uint32_t item_id);

/**
 * @brief Encode a Set request for an int item
 */
int test_build_set_int(uint8_t* out_buf, size_t out_buf_size, uint32_t item_id, int32_t value);

/**
 * @brief Encode a Set request for a string item
 */
int test_build_set_string(uint8_t* out_buf, size_t out_buf_size, uint32_t item_id, const char* value);

/**
 * @brief Encode a Multi-Get request
 */
int test_build_multi_get(uint8_t* out_buf, size_t out_buf_size, const uint32_t* ids, uint8_t count);

/**
 * @brief Encode a Multi-Set request with two int items
 */
int test_build_multi_set_ints(
    uint8_t* out_buf, size_t out_buf_size, uint32_t id1, int32_t val1, uint32_t id2, int32_t val2);

/**
 * @brief Reset shared test state between tests
 */
void test_reset(void);
