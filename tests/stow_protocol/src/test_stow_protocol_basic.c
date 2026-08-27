/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "gantry/stow/stow_serde.h"
#include "test_common.h"

#define SESSION_ID 11

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

ZTEST_SUITE(stow_protocol_basic, NULL, NULL, before_each, after_each, NULL);

static void open_session(stow_role_t roles)
{
    int ret = stow_protocol_session_open(SESSION_ID, roles);
    zassert_equal(ret, 0, "session_open failed: %d", ret);
}

/**
 * @brief Submit a request and return the decoded outer cmd of the response
 */
static uint32_t submit_and_get_cmd(const uint8_t* data, size_t len, stow_role_t roles)
{
    int ret = test_submit_rx(SESSION_ID, roles, data, len);
    zassert_equal(ret, 0);

    struct test_capture cap = test_await_tx(K_SECONDS(1));
    zassert_not_null(cap.buf, "no TX captured");
    zassert_equal(cap.session_id, SESSION_ID);

    uint32_t cmd;
    zcbor_state_t dec[4];
    zassert_true(test_decode_response_header(cap.buf, &cmd, dec, ARRAY_SIZE(dec)));
    net_buf_unref(cap.buf);
    return cmd;
}

//**********************************************************
//* Version
//**********************************************************

ZTEST(stow_protocol_basic, test_version_request)
{
    open_session(STOW_ROLE_GUEST);

    uint8_t buf[8];
    int len = test_build_simple(buf, sizeof(buf), STOW_MSG_VERSION);
    int ret = test_submit_rx(SESSION_ID, STOW_ROLE_GUEST, buf, len);
    zassert_equal(ret, 0);

    struct test_capture cap = test_await_tx(K_SECONDS(1));
    zassert_not_null(cap.buf);
    zassert_equal(cap.session_id, SESSION_ID);

    uint32_t cmd;
    zcbor_state_t dec[4];
    zassert_true(test_decode_response_header(cap.buf, &cmd, dec, ARRAY_SIZE(dec)));
    zassert_equal(cmd, STOW_MSG_VERSION_RESPONSE);
    uint32_t version;
    zassert_true(zcbor_uint32_decode(dec, &version));
    zassert_equal(version, STOW_PROTOCOL_VERSION);
    net_buf_unref(cap.buf);
}

//**********************************************************
//* Get
//**********************************************************

ZTEST(stow_protocol_basic, test_get_int_returns_current_value)
{
    open_session(STOW_ROLE_GUEST);

    // Seed the value so we know what to expect.
    data_value_t v = { .type = STOW_ITEM_TYPE_INT, .data.int_value = 27 };
    zassert_equal(stow_set(STOW_ID_TEST_INT, v), 0);
    test_sync();  // ensure any update events fan out before our Get

    test_drain_captures();

    uint8_t req[16];
    int len = test_build_with_id(req, sizeof(req), STOW_MSG_GET, STOW_ID_TEST_INT);
    int ret = test_submit_rx(SESSION_ID, STOW_ROLE_GUEST, req, len);
    zassert_equal(ret, 0);

    struct test_capture cap = test_await_tx(K_SECONDS(1));
    zassert_not_null(cap.buf);

    uint32_t cmd;
    zcbor_state_t dec[4];
    zassert_true(test_decode_response_header(cap.buf, &cmd, dec, ARRAY_SIZE(dec)));
    zassert_equal(cmd, STOW_MSG_GET_RESPONSE, "expected Get Response (got %u)", cmd);
    uint32_t id;
    zassert_true(zcbor_uint32_decode(dec, &id));
    zassert_equal(id, STOW_ID_TEST_INT);
    int32_t value;
    zassert_true(zcbor_int32_decode(dec, &value));
    zassert_equal(value, 27);
    net_buf_unref(cap.buf);
}

ZTEST(stow_protocol_basic, test_get_permission_denied)
{
    open_session(STOW_ROLE_GUEST);

    uint8_t req[16];
    int len = test_build_with_id(req, sizeof(req), STOW_MSG_GET, STOW_ID_TEST_SESSION_INT);
    uint32_t cmd = submit_and_get_cmd(req, len, STOW_ROLE_GUEST);
    zassert_equal(cmd, STOW_MSG_ERROR, "expected Error (got %u)", cmd);
}

//**********************************************************
//* Set
//**********************************************************

ZTEST(stow_protocol_basic, test_set_int_changes_value)
{
    open_session(STOW_ROLE_GUEST);

    uint8_t req[16];
    int len = test_build_set_int(req, sizeof(req), STOW_ID_TEST_INT, -42);
    uint32_t cmd = submit_and_get_cmd(req, len, STOW_ROLE_GUEST);
    zassert_equal(cmd, STOW_MSG_OK, "expected OK (got %u)", cmd);

    data_value_t v = { 0 };
    zassert_equal(stow_get(STOW_ID_TEST_INT, &v), 0);
    zassert_equal(v.data.int_value, -42);
    stow_release(STOW_ID_TEST_INT, &v);
}

ZTEST(stow_protocol_basic, test_set_string_changes_value)
{
    open_session(STOW_ROLE_SESSION);

    uint8_t req[64];
    int len = test_build_set_string(req, sizeof(req), STOW_ID_TEST_STRING, "newval");
    uint32_t cmd = submit_and_get_cmd(req, len, STOW_ROLE_SESSION);
    zassert_equal(cmd, STOW_MSG_OK);

    data_value_t v = { 0 };
    zassert_equal(stow_get(STOW_ID_TEST_STRING, &v), 0);
    zassert_str_equal(v.data.string_value, "newval");
    stow_release(STOW_ID_TEST_STRING, &v);
}

ZTEST(stow_protocol_basic, test_set_permission_denied)
{
    open_session(STOW_ROLE_GUEST);

    // TestString requires SESSION write; STOW_ROLE_GUEST should be rejected.
    uint8_t req[64];
    int len = test_build_set_string(req, sizeof(req), STOW_ID_TEST_STRING, "denied");
    uint32_t cmd = submit_and_get_cmd(req, len, STOW_ROLE_GUEST);
    zassert_equal(cmd, STOW_MSG_ERROR, "expected Error (got %u)", cmd);
}

ZTEST(stow_protocol_basic, test_set_invalid_value_returns_error)
{
    open_session(STOW_ROLE_GUEST);

    // TestInt constraints: min=-100, max=100. 500 is out of range.
    uint8_t req[16];
    int len = test_build_set_int(req, sizeof(req), STOW_ID_TEST_INT, 500);
    uint32_t cmd = submit_and_get_cmd(req, len, STOW_ROLE_GUEST);
    zassert_equal(cmd, STOW_MSG_ERROR);
}

//**********************************************************
//* Error paths
//**********************************************************

ZTEST(stow_protocol_basic, test_unknown_command_returns_error)
{
    open_session(STOW_ROLE_GUEST);

    uint8_t req[8];
    int len = test_build_simple(req, sizeof(req), 250);
    uint32_t cmd = submit_and_get_cmd(req, len, STOW_ROLE_GUEST);
    zassert_equal(cmd, STOW_MSG_ERROR);
}

ZTEST(stow_protocol_basic, test_malformed_request_returns_error)
{
    open_session(STOW_ROLE_GUEST);

    uint8_t bogus[] = { 0xFF, 0xFE, 0xFD };
    uint32_t cmd = submit_and_get_cmd(bogus, sizeof(bogus), STOW_ROLE_GUEST);
    zassert_equal(cmd, STOW_MSG_ERROR);
}

//**********************************************************
//* Session admission
//**********************************************************

ZTEST(stow_protocol_basic, test_session_open_duplicate_returns_ealready)
{
    open_session(STOW_ROLE_GUEST);
    int ret = stow_protocol_session_open(SESSION_ID, STOW_ROLE_GUEST);
    zassert_equal(ret, -EALREADY);
}

//**********************************************************
//* OK
//**********************************************************

ZTEST(stow_protocol_basic, test_ok_response)
{
    open_session(STOW_ROLE_GUEST);

    uint8_t req[16];
    int len = test_build_set_int(req, sizeof(req), STOW_ID_TEST_INT, 5);
    zassert_equal(test_submit_rx(SESSION_ID, STOW_ROLE_GUEST, req, len), 0);

    struct test_capture cap = test_await_tx(K_SECONDS(1));
    zassert_not_null(cap.buf);

    uint32_t cmd;
    zcbor_state_t dec[4];
    zassert_true(test_decode_response_header(cap.buf, &cmd, dec, ARRAY_SIZE(dec)));
    zassert_equal(cmd, STOW_MSG_OK);
    zassert_true(zcbor_list_end_decode(dec));
    net_buf_unref(cap.buf);
}

//**********************************************************
//* Error
//**********************************************************

ZTEST(stow_protocol_basic, test_error_contains_code)
{
    open_session(STOW_ROLE_GUEST);

    uint8_t req[16];
    int len = test_build_with_id(req, sizeof(req), STOW_MSG_GET, STOW_ID_TEST_SESSION_INT);
    zassert_equal(test_submit_rx(SESSION_ID, STOW_ROLE_GUEST, req, len), 0);

    struct test_capture cap = test_await_tx(K_SECONDS(1));
    zassert_not_null(cap.buf);

    uint32_t cmd;
    zcbor_state_t dec[4];
    zassert_true(test_decode_response_header(cap.buf, &cmd, dec, ARRAY_SIZE(dec)));
    zassert_equal(cmd, STOW_MSG_ERROR);
    uint32_t code;
    zassert_true(zcbor_uint32_decode(dec, &code));
    zassert_equal(code, STOW_ERR_PERMISSION_DENIED);
    net_buf_unref(cap.buf);
}

//**********************************************************
//* Multi-Get
//**********************************************************

ZTEST(stow_protocol_basic, test_multi_get_returns_all_values)
{
    open_session(STOW_ROLE_GUEST);

    data_value_t v = { .type = STOW_ITEM_TYPE_INT, .data.int_value = 55 };
    zassert_equal(stow_set(STOW_ID_TEST_INT, v), 0);
    test_sync();
    test_drain_captures();

    uint8_t req[32];
    uint32_t ids[] = { STOW_ID_TEST_INT, STOW_ID_TEST_INT };
    int len = test_build_multi_get(req, sizeof(req), ids, 2);
    zassert_true(len > 0);
    zassert_equal(test_submit_rx(SESSION_ID, STOW_ROLE_GUEST, req, len), 0);

    struct test_capture cap = test_await_tx(K_SECONDS(1));
    zassert_not_null(cap.buf);

    uint32_t cmd;
    zcbor_state_t dec[4];
    zassert_true(test_decode_response_header(cap.buf, &cmd, dec, ARRAY_SIZE(dec)));
    zassert_equal(cmd, STOW_MSG_MULTI_GET_RESPONSE, "expected Multi-Get Response cmd 14 (got %u)", cmd);

    uint32_t id0;
    zassert_true(zcbor_uint32_decode(dec, &id0));
    zassert_equal(id0, STOW_ID_TEST_INT);
    int32_t val0;
    zassert_true(zcbor_int32_decode(dec, &val0));
    zassert_equal(val0, 55);

    uint32_t id1;
    zassert_true(zcbor_uint32_decode(dec, &id1));
    zassert_equal(id1, STOW_ID_TEST_INT);
    int32_t val1;
    zassert_true(zcbor_int32_decode(dec, &val1));
    zassert_equal(val1, 55);

    net_buf_unref(cap.buf);
}

ZTEST(stow_protocol_basic, test_multi_get_permission_denied_returns_error)
{
    open_session(STOW_ROLE_GUEST);

    uint8_t req[32];
    uint32_t ids[] = { STOW_ID_TEST_SESSION_INT };
    int len = test_build_multi_get(req, sizeof(req), ids, 1);
    uint32_t cmd = submit_and_get_cmd(req, len, STOW_ROLE_GUEST);
    zassert_equal(cmd, STOW_MSG_ERROR, "expected Error (got %u)", cmd);
}

//**********************************************************
//* Multi-Set
//**********************************************************

ZTEST(stow_protocol_basic, test_multi_set_changes_both_values)
{
    open_session(STOW_ROLE_GUEST);

    uint8_t req[32];
    int len = test_build_multi_set_ints(req, sizeof(req), STOW_ID_TEST_INT, 10, STOW_ID_TEST_INT, 20);
    zassert_true(len > 0);
    zassert_equal(test_submit_rx(SESSION_ID, STOW_ROLE_GUEST, req, len), 0);

    // Expect a single OK response
    struct test_capture cap = test_await_tx(K_SECONDS(1));
    zassert_not_null(cap.buf);
    uint32_t cmd;
    zcbor_state_t dec[4];
    zassert_true(test_decode_response_header(cap.buf, &cmd, dec, ARRAY_SIZE(dec)));
    zassert_equal(cmd, STOW_MSG_OK, "expected OK (got %u)", cmd);
    net_buf_unref(cap.buf);

    // Final value should be 20 (last write wins)
    data_value_t v = { 0 };
    zassert_equal(stow_get(STOW_ID_TEST_INT, &v), 0);
    zassert_equal(v.data.int_value, 20);
    stow_release(STOW_ID_TEST_INT, &v);
}

ZTEST(stow_protocol_basic, test_multi_set_first_error_stops)
{
    open_session(STOW_ROLE_GUEST);

    // First item: invalid value (out of range). Second item: valid but not reached.
    uint8_t req[32];
    int len = test_build_multi_set_ints(req, sizeof(req), STOW_ID_TEST_INT, 500, STOW_ID_TEST_INT, 30);
    zassert_true(len > 0);
    zassert_equal(test_submit_rx(SESSION_ID, STOW_ROLE_GUEST, req, len), 0);

    // Expect a single Error response
    struct test_capture cap = test_await_tx(K_SECONDS(1));
    zassert_not_null(cap.buf);
    uint32_t cmd;
    zcbor_state_t dec[4];
    zassert_true(test_decode_response_header(cap.buf, &cmd, dec, ARRAY_SIZE(dec)));
    zassert_equal(cmd, STOW_MSG_ERROR, "expected Error (got %u)", cmd);
    net_buf_unref(cap.buf);

    // No second response should arrive
    struct test_capture extra = test_await_tx(K_MSEC(100));
    zassert_is_null(extra.buf, "unexpected second response after MSET error");
}

ZTEST(stow_protocol_basic, test_pipeline_two_messages)
{
    open_session(STOW_ROLE_GUEST);

    uint8_t combined[32];
    int l1 = test_build_simple(combined, sizeof(combined), STOW_MSG_VERSION);
    int l2 = test_build_simple(combined + l1, sizeof(combined) - l1, STOW_MSG_VERSION);
    zassert_true(l1 > 0 && l2 > 0);
    zassert_equal(test_submit_rx(SESSION_ID, STOW_ROLE_GUEST, combined, (size_t)(l1 + l2)), 0);

    struct test_capture cap1 = test_await_tx(K_SECONDS(1));
    zassert_not_null(cap1.buf);
    uint32_t cmd1;
    zcbor_state_t dec1[4];
    zassert_true(test_decode_response_header(cap1.buf, &cmd1, dec1, ARRAY_SIZE(dec1)));
    zassert_equal(cmd1, STOW_MSG_VERSION_RESPONSE);
    net_buf_unref(cap1.buf);

    struct test_capture cap2 = test_await_tx(K_SECONDS(1));
    zassert_not_null(cap2.buf);
    uint32_t cmd2;
    zcbor_state_t dec2[4];
    zassert_true(test_decode_response_header(cap2.buf, &cmd2, dec2, ARRAY_SIZE(dec2)));
    zassert_equal(cmd2, STOW_MSG_VERSION_RESPONSE);
    net_buf_unref(cap2.buf);
}
