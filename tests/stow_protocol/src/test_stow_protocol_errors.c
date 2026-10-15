/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "test_common.h"

#define SESSION_ID 81

static void before_each(void* fixture)
{
    (void)fixture;
    test_reset();
    test_open_session(SESSION_ID, STOW_ROLE_GUEST);
}

static void after_each(void* fixture)
{
    (void)fixture;
    test_endpoint_close(SESSION_ID);
    test_sync();
    test_drain_captures();
    stow_init();
}

ZTEST_SUITE(stow_protocol_errors, NULL, NULL, before_each, after_each, NULL);

static struct net_buf* exchange(const uint8_t* request, size_t len)
{
    zassert_equal(test_submit_rx(SESSION_ID, request, len), 0);
    struct test_capture cap = test_await_tx(K_SECONDS(1));
    zassert_not_null(cap.buf, "no response");
    zassert_equal(cap.session_id, SESSION_ID);
    return cap.buf;
}

static struct stow_serde_error exchange_error(const uint8_t* request, size_t len)
{
    struct net_buf* buf = exchange(request, len);
    struct stow_serde_error error;
    zassert_true(test_decode_error(buf, &error), "response is not an Error");
    net_buf_unref(buf);
    return error;
}

static uint32_t exchange_tag(const uint8_t* request, size_t len, uint32_t expected_cmd)
{
    struct net_buf* buf = exchange(request, len);
    uint32_t cmd = 0;
    uint32_t tag = 0;
    zcbor_state_t dec[4];
    zassert_true(test_decode_response_header(buf, &cmd, &tag, dec, ARRAY_SIZE(dec)));
    zassert_equal(cmd, expected_cmd, "expected %u, got %u", expected_cmd, cmd);
    net_buf_unref(buf);
    return tag;
}

//**********************************************************
//* Tags
//**********************************************************

ZTEST(stow_protocol_errors, test_every_response_echoes_the_tag)
{
    uint8_t req[32];
    uint32_t ids[] = { STOW_ID_TEST_INT };

    int len = test_build_simple(req, sizeof(req), STOW_MSG_HELLO, 0);
    zassert_equal(exchange_tag(req, len, STOW_MSG_HELLO_RESPONSE), 0);

    len = test_build_with_id(req, sizeof(req), STOW_MSG_DESCRIBE, 1, 0);
    zassert_equal(exchange_tag(req, len, STOW_MSG_DESCRIBE_RESPONSE), 1);

    len = test_build_with_id(req, sizeof(req), STOW_MSG_GET, 2, STOW_ID_TEST_INT);
    zassert_equal(exchange_tag(req, len, STOW_MSG_GET_RESPONSE), 2);

    len = test_build_set_int(req, sizeof(req), 3, STOW_ID_TEST_INT, 1);
    zassert_equal(exchange_tag(req, len, STOW_MSG_OK), 3);

    len = test_build_list(req, sizeof(req), STOW_MSG_GET, 4, ids, 1);
    zassert_equal(exchange_tag(req, len, STOW_MSG_GET_RESPONSE), 4);

    len = test_build_set_ints(req, sizeof(req), 5, STOW_ID_TEST_INT, 1, STOW_ID_TEST_INT, 2);
    zassert_equal(exchange_tag(req, len, STOW_MSG_OK), 5);

    len = test_build_with_id(req, sizeof(req), STOW_MSG_SUBSCRIBE, 6, STOW_ID_TEST_INT);
    zassert_equal(exchange_tag(req, len, STOW_MSG_OK), 6);

    len = test_build_with_id(req, sizeof(req), STOW_MSG_UNSUBSCRIBE, STOW_TAG_MAX, STOW_ID_TEST_INT);
    zassert_equal(exchange_tag(req, len, STOW_MSG_OK), STOW_TAG_MAX);

    len = test_build_with_id(req, sizeof(req), STOW_MSG_GET, 8, STOW_ID_TEST_SESSION_INT);
    zassert_equal(exchange_tag(req, len, STOW_MSG_ERROR), 8);

    len = test_build_simple(req, sizeof(req), STOW_MSG_HELLO, 9);
    zassert_equal(exchange_tag(req, len, STOW_MSG_HELLO_RESPONSE), 9);
}

ZTEST(stow_protocol_errors, test_undecodable_request_has_null_tag)
{
    const uint8_t bogus[] = { 0xFF, 0xFE, 0xFD };
    struct stow_serde_error error = exchange_error(bogus, sizeof(bogus));
    zassert_equal(error.code, STOW_ERR_MALFORMED_MSG);
    zassert_equal(error.tag, STOW_TAG_NONE);
}

ZTEST(stow_protocol_errors, test_tag_out_of_range_is_malformed_with_null_tag)
{
    uint8_t req[8];
    int len = test_build_simple(req, sizeof(req), STOW_MSG_HELLO, STOW_TAG_MAX + 1U);
    struct stow_serde_error error = exchange_error(req, len);
    zassert_equal(error.code, STOW_ERR_MALFORMED_MSG);
    zassert_equal(error.tag, STOW_TAG_NONE);
}

ZTEST(stow_protocol_errors, test_tag_that_is_not_a_uint_is_malformed_with_null_tag)
{
    const uint8_t req[] = { 0x82, 0x00, 0x20 };
    struct stow_serde_error error = exchange_error(req, sizeof(req));
    zassert_equal(error.code, STOW_ERR_MALFORMED_MSG);
    zassert_equal(error.tag, STOW_TAG_NONE);
}

ZTEST(stow_protocol_errors, test_unknown_message_echoes_the_tag)
{
    uint8_t req[8];
    int len = test_build_simple(req, sizeof(req), 250, 9);
    struct stow_serde_error error = exchange_error(req, len);
    zassert_equal(error.code, STOW_ERR_UNKNOWN_MSG);
    zassert_equal(error.tag, 9);
}

//**********************************************************
//* Error codes and fields
//**********************************************************

ZTEST(stow_protocol_errors, test_unknown_item_is_named)
{
    uint8_t req[16];
    int len = test_build_with_id(req, sizeof(req), STOW_MSG_GET, TEST_TAG, STOW_ID_COUNT + 3);
    struct stow_serde_error error = exchange_error(req, len);
    zassert_equal(error.code, STOW_ERR_INVALID_ITEM);
    zassert_equal(error.item, STOW_ID_COUNT + 3);
    zassert_equal(error.index, 0);
}

ZTEST(stow_protocol_errors, test_rejected_value_is_invalid_value)
{
    uint8_t req[16];
    int len = test_build_set_int(req, sizeof(req), TEST_TAG, STOW_ID_TEST_INT, 500);
    struct stow_serde_error error = exchange_error(req, len);
    zassert_equal(error.code, STOW_ERR_INVALID_VALUE);
    zassert_equal(error.tag, TEST_TAG);
    zassert_equal(error.item, STOW_ID_TEST_INT);
}

ZTEST(stow_protocol_errors, test_permission_denied_names_the_item)
{
    uint8_t req[16];
    int len = test_build_with_id(req, sizeof(req), STOW_MSG_GET, TEST_TAG, STOW_ID_TEST_SESSION_INT);
    struct stow_serde_error error = exchange_error(req, len);
    zassert_equal(error.code, STOW_ERR_PERMISSION_DENIED);
    zassert_equal(error.item, STOW_ID_TEST_SESSION_INT);
}

ZTEST(stow_protocol_errors, test_second_tofu_write_is_already_set)
{
    uint8_t req[32];
    int len = test_build_set_string(req, sizeof(req), 1, STOW_ID_TEST_TOFU, "first");
    zassert_equal(exchange_tag(req, len, STOW_MSG_OK), 1);

    len = test_build_set_string(req, sizeof(req), 2, STOW_ID_TEST_TOFU, "second");
    struct stow_serde_error error = exchange_error(req, len);
    zassert_equal(error.code, STOW_ERR_ALREADY_SET);
    zassert_equal(error.tag, 2);
    zassert_equal(error.item, STOW_ID_TEST_TOFU);
}

ZTEST(stow_protocol_errors, test_set_failure_names_entry)
{
    uint8_t req[32];
    int len = test_build_set_ints(req, sizeof(req), TEST_TAG, STOW_ID_TEST_INT, 10, STOW_ID_TEST_INT, 500);
    struct stow_serde_error error = exchange_error(req, len);
    zassert_equal(error.code, STOW_ERR_INVALID_VALUE);
    zassert_equal(error.item, STOW_ID_TEST_INT);
    zassert_equal(error.index, 1);

    data_value_t value = { 0 };
    zassert_equal(stow_get(STOW_ID_TEST_INT, &value), 0);
    zassert_equal(value.data.int_value, 10, "entry before the failure was not applied");
}

ZTEST(stow_protocol_errors, test_set_applies_entries_before_an_unknown_item)
{
    uint8_t req[32];
    int len = test_build_set_ints(req, sizeof(req), TEST_TAG, STOW_ID_TEST_INT, 12, STOW_ID_COUNT, 1);
    struct stow_serde_error error = exchange_error(req, len);
    zassert_equal(error.code, STOW_ERR_INVALID_ITEM);
    zassert_equal(error.item, STOW_ID_COUNT);
    zassert_equal(error.index, 1);

    data_value_t value = { 0 };
    zassert_equal(stow_get(STOW_ID_TEST_INT, &value), 0);
    zassert_equal(value.data.int_value, 12, "entry before the failure was not applied");
}

ZTEST(stow_protocol_errors, test_set_with_a_mistyped_value_names_the_item_and_applies_nothing)
{
    uint8_t req[32];
    zcbor_state_t enc[4];
    zcbor_new_encode_state(enc, ARRAY_SIZE(enc), req, sizeof(req), 1);
    struct zcbor_string text = { .value = "x", .len = 1 };
    zassert_true(
        zcbor_list_start_encode(enc, 6) && zcbor_uint32_put(enc, STOW_MSG_SET) && zcbor_uint32_put(enc, TEST_TAG) &&
        zcbor_uint32_put(enc, STOW_ID_TEST_INT) && zcbor_int32_put(enc, 14) &&
        zcbor_uint32_put(enc, STOW_ID_TEST_SESSION_INT) && zcbor_tstr_encode(enc, &text) &&
        zcbor_list_end_encode(enc, 6));

    struct stow_serde_error error = exchange_error(req, enc[0].payload - req);
    zassert_equal(error.code, STOW_ERR_MALFORMED_MSG);
    zassert_equal(error.item, STOW_ID_TEST_SESSION_INT);
    zassert_equal(error.index, STOW_SERDE_ABSENT);

    data_value_t value = { 0 };
    zassert_equal(stow_get(STOW_ID_TEST_INT, &value), 0);
    zassert_equal(value.data.int_value, 0, "a malformed request was applied");
}

ZTEST(stow_protocol_errors, test_set_with_trailing_bytes_applies_nothing)
{
    uint8_t req[32];
    int len = test_build_set_ints(req, sizeof(req), TEST_TAG, STOW_ID_TEST_INT, 21, STOW_ID_TEST_INT, 22);
    req[len++] = 0x00;

    struct stow_serde_error error = exchange_error(req, len);
    zassert_equal(error.code, STOW_ERR_MALFORMED_MSG);
    zassert_equal(error.tag, TEST_TAG);
    zassert_equal(error.index, STOW_SERDE_ABSENT);

    data_value_t value = { 0 };
    zassert_equal(stow_get(STOW_ID_TEST_INT, &value), 0);
    zassert_equal(value.data.int_value, 0, "a request with trailing bytes was applied");
}

ZTEST(stow_protocol_errors, test_set_without_array_end_applies_nothing)
{
    const uint8_t req[] = { 0x9F, 0x06, 0x11, 0x01, 0x0A, 0x01, 0x0B };

    struct stow_serde_error error = exchange_error(req, sizeof(req));
    zassert_equal(error.code, STOW_ERR_MALFORMED_MSG);
    zassert_equal(error.tag, TEST_TAG);
    zassert_equal(error.index, STOW_SERDE_ABSENT);

    data_value_t value = { 0 };
    zassert_equal(stow_get(STOW_ID_TEST_INT, &value), 0);
    zassert_equal(value.data.int_value, 0, "a malformed request was applied");
}

ZTEST(stow_protocol_errors, test_get_failure_names_entry)
{
    uint8_t req[32];
    uint32_t ids[] = { STOW_ID_TEST_INT, STOW_ID_TEST_STRING, STOW_ID_TEST_DEV_INT };
    int len = test_build_list(req, sizeof(req), STOW_MSG_GET, TEST_TAG, ids, ARRAY_SIZE(ids));
    struct stow_serde_error error = exchange_error(req, len);
    zassert_equal(error.code, STOW_ERR_PERMISSION_DENIED);
    zassert_equal(error.item, STOW_ID_TEST_DEV_INT);
    zassert_equal(error.index, 2);
}

ZTEST(stow_protocol_errors, test_oversized_response_is_too_large)
{
    char long_value[351];
    memset(long_value, 'x', sizeof(long_value) - 1);
    long_value[sizeof(long_value) - 1] = '\0';
    data_value_t value = { .type = STOW_ITEM_TYPE_STRING, .data.string_value = long_value };
    zassert_equal(stow_set(STOW_ID_TEST_LONG_STRING, value), 0);

    uint8_t req[16];
    int len = test_build_with_id(req, sizeof(req), STOW_MSG_GET, TEST_TAG, STOW_ID_TEST_LONG_STRING);
    struct stow_serde_error error = exchange_error(req, len);
    zassert_equal(error.code, STOW_ERR_TOO_LARGE);
    zassert_equal(error.tag, TEST_TAG);
    zassert_equal(error.item, STOW_ID_TEST_LONG_STRING);
}
