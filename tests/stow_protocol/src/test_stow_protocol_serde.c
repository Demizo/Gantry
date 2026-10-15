/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>

#include "gantry/stow/stow_serde.h"
#include "test_common.h"

static void reset_stow_only(void* fixture)
{
    (void)fixture;
    stow_init();
}

ZTEST_SUITE(stow_protocol_serde, NULL, NULL, reset_stow_only, NULL, NULL);

//**********************************************************
//* Decode tests
//**********************************************************

ZTEST(stow_protocol_serde, test_decode_hello_request)
{
    uint8_t buf[8];
    int len = test_build_simple(buf, sizeof(buf), STOW_MSG_HELLO, TEST_TAG);
    zassert_true(len > 0);

    zcbor_state_t dec[4];
    zcbor_new_decode_state(dec, ARRAY_SIZE(dec), buf, len, 1, NULL, 0);

    struct stow_serde_request req = { 0 };
    zassert_equal(stow_serde_decode_request(dec, &req), 0);
    zassert_equal(req.message_code, STOW_MSG_HELLO);
    zassert_equal(req.tag, TEST_TAG);
}

ZTEST(stow_protocol_serde, test_decode_describe_request)
{
    uint8_t buf[8];
    int len = test_build_with_id(buf, sizeof(buf), STOW_MSG_DESCRIBE, TEST_TAG, 0);
    zcbor_state_t dec[4];
    zcbor_new_decode_state(dec, ARRAY_SIZE(dec), buf, len, 1, NULL, 0);

    struct stow_serde_request req = { 0 };
    zassert_equal(stow_serde_decode_request(dec, &req), 0);
    zassert_equal(req.message_code, STOW_MSG_DESCRIBE);
}

ZTEST(stow_protocol_serde, test_decode_get_request)
{
    uint8_t buf[16];
    int len = test_build_with_id(buf, sizeof(buf), STOW_MSG_GET, TEST_TAG, STOW_ID_TEST_INT);
    zcbor_state_t dec[4];
    zcbor_new_decode_state(dec, ARRAY_SIZE(dec), buf, len, 1, NULL, 0);

    struct stow_serde_request req = { 0 };
    zassert_equal(stow_serde_decode_request(dec, &req), 0);
    zassert_equal(req.message_code, STOW_MSG_GET);
    zassert_equal(req.entry_count, 1);

    zcbor_state_t entries[4];
    stow_serde_entries_start(&req, entries, ARRAY_SIZE(entries));
    uint32_t id = 0;
    zassert_equal(stow_serde_decode_entry(entries, &id), 0);
    zassert_equal(id, STOW_ID_TEST_INT);
}

ZTEST(stow_protocol_serde, test_decode_set_request_int)
{
    uint8_t buf[32];
    int len = test_build_set_int(buf, sizeof(buf), TEST_TAG, STOW_ID_TEST_INT, 42);
    zcbor_state_t dec[4];
    zcbor_new_decode_state(dec, ARRAY_SIZE(dec), buf, len, 1, NULL, 0);

    struct stow_serde_request req = { 0 };
    zassert_equal(stow_serde_decode_request(dec, &req), 0);
    zassert_equal(req.message_code, STOW_MSG_SET);
    zassert_equal(req.entry_count, 1);

    zcbor_state_t entries[4];
    stow_serde_entries_start(&req, entries, ARRAY_SIZE(entries));
    uint32_t id = 0;
    zassert_equal(stow_serde_decode_entry(entries, &id), 0);
    zassert_equal(id, STOW_ID_TEST_INT);
    data_value_t value = { 0 };
    zassert_equal(stow_decode(entries, STOW_ID_TEST_INT, &value), 0);
    zassert_equal(value.type, STOW_ITEM_TYPE_INT);
    zassert_equal(value.data.int_value, 42);
    stow_release(STOW_ID_TEST_INT, &value);
}

ZTEST(stow_protocol_serde, test_decode_set_request_string)
{
    uint8_t buf[32];
    int len = test_build_set_string(buf, sizeof(buf), TEST_TAG, STOW_ID_TEST_STRING, "world");
    zcbor_state_t dec[4];
    zcbor_new_decode_state(dec, ARRAY_SIZE(dec), buf, len, 1, NULL, 0);

    struct stow_serde_request req = { 0 };
    zassert_equal(stow_serde_decode_request(dec, &req), 0);
    zassert_equal(req.message_code, STOW_MSG_SET);
    zassert_equal(req.entry_count, 1);

    zcbor_state_t entries[4];
    stow_serde_entries_start(&req, entries, ARRAY_SIZE(entries));
    uint32_t id = 0;
    zassert_equal(stow_serde_decode_entry(entries, &id), 0);
    zassert_equal(id, STOW_ID_TEST_STRING);
    data_value_t value = { 0 };
    zassert_equal(stow_decode(entries, STOW_ID_TEST_STRING, &value), 0);
    zassert_equal(value.type, STOW_ITEM_TYPE_STRING);
    zassert_str_equal(value.data.string_value, "world");
    stow_release(STOW_ID_TEST_STRING, &value);
}

ZTEST(stow_protocol_serde, test_decode_subscribe_request)
{
    uint8_t buf[16];
    int len = test_build_with_id(buf, sizeof(buf), STOW_MSG_SUBSCRIBE, TEST_TAG, STOW_ID_TEST_INT);
    zcbor_state_t dec[4];
    zcbor_new_decode_state(dec, ARRAY_SIZE(dec), buf, len, 1, NULL, 0);

    struct stow_serde_request req = { 0 };
    zassert_equal(stow_serde_decode_request(dec, &req), 0);
    zassert_equal(req.message_code, STOW_MSG_SUBSCRIBE);
    zassert_equal(req.entry_count, 1);

    zcbor_state_t entries[4];
    stow_serde_entries_start(&req, entries, ARRAY_SIZE(entries));
    uint32_t id = 0;
    zassert_equal(stow_serde_decode_entry(entries, &id), 0);
    zassert_equal(id, STOW_ID_TEST_INT);
}

ZTEST(stow_protocol_serde, test_decode_unknown_command_returns_enotsup)
{
    uint8_t buf[8];
    int len = test_build_simple(buf, sizeof(buf), 99, TEST_TAG);
    zcbor_state_t dec[4];
    zcbor_new_decode_state(dec, ARRAY_SIZE(dec), buf, len, 1, NULL, 0);

    struct stow_serde_request req = { 0 };
    int ret = stow_serde_decode_request(dec, &req);
    zassert_equal(ret, -ENOTSUP);
}

ZTEST(stow_protocol_serde, test_decode_leaves_unknown_items_to_the_entries)
{
    uint8_t buf[16];
    int len = test_build_with_id(buf, sizeof(buf), STOW_MSG_GET, TEST_TAG, STOW_ID_COUNT + 5);
    zcbor_state_t dec[4];
    zcbor_new_decode_state(dec, ARRAY_SIZE(dec), buf, len, 1, NULL, 0);

    struct stow_serde_request req = { 0 };
    zassert_equal(stow_serde_decode_request(dec, &req), 0);

    zcbor_state_t entries[4];
    stow_serde_entries_start(&req, entries, ARRAY_SIZE(entries));
    uint32_t id = 0;
    zassert_equal(stow_serde_decode_entry(entries, &id), -ENOENT);
    zassert_equal(id, STOW_ID_COUNT + 5);
}

ZTEST(stow_protocol_serde, test_decode_malformed_returns_ebadmsg)
{
    uint8_t bogus[] = { 0xFF, 0xFF, 0xFF };
    zcbor_state_t dec[4];
    zcbor_new_decode_state(dec, ARRAY_SIZE(dec), bogus, sizeof(bogus), 1, NULL, 0);

    struct stow_serde_request req = { 0 };
    int ret = stow_serde_decode_request(dec, &req);
    zassert_equal(ret, -EBADMSG);
}

//**********************************************************
//* Encode tests
//**********************************************************

ZTEST(stow_protocol_serde, test_encode_ok)
{
    uint8_t buf[16];
    zcbor_state_t enc[4];
    zcbor_new_encode_state(enc, ARRAY_SIZE(enc), buf, sizeof(buf), 1);

    int ret = stow_serde_encode_ok(enc, TEST_TAG);
    zassert_equal(ret, 0);

    size_t encoded_len = enc[0].payload - buf;
    zcbor_state_t dec[4];
    zcbor_new_decode_state(dec, ARRAY_SIZE(dec), buf, encoded_len, 1, NULL, 0);
    zassert_true(zcbor_list_start_decode(dec));
    uint32_t cmd;
    zassert_true(zcbor_uint32_decode(dec, &cmd));
    zassert_equal(cmd, STOW_MSG_OK);
    uint32_t tag;
    zassert_true(zcbor_uint32_decode(dec, &tag));
    zassert_equal(tag, TEST_TAG);
    zassert_true(zcbor_list_end_decode(dec));
}

ZTEST(stow_protocol_serde, test_encode_error)
{
    uint8_t buf[16];
    zcbor_state_t enc[4];
    zcbor_new_encode_state(enc, ARRAY_SIZE(enc), buf, sizeof(buf), 1);

    struct stow_serde_error error = {
        .code = STOW_ERR_PERMISSION_DENIED, .tag = TEST_TAG, .item = 3, .index = STOW_SERDE_ABSENT
    };
    zassert_equal(stow_wire_encode_error(enc, &error), 0);

    const uint8_t expected[] = { 0x84, 0x0B, 0x11, 0x05, 0x03 };
    zassert_equal(enc[0].payload - buf, sizeof(expected));
    zassert_mem_equal(buf, expected, sizeof(expected));
}

ZTEST(stow_protocol_serde, test_encode_error_without_tag_or_item)
{
    uint8_t buf[16];
    zcbor_state_t enc[4];
    zcbor_new_encode_state(enc, ARRAY_SIZE(enc), buf, sizeof(buf), 1);

    struct stow_serde_error error = {
        .code = STOW_ERR_MALFORMED_MSG, .tag = STOW_TAG_NONE, .item = STOW_SERDE_ABSENT, .index = STOW_SERDE_ABSENT
    };
    zassert_equal(stow_wire_encode_error(enc, &error), 0);

    const uint8_t expected[] = { 0x83, 0x0B, 0xF6, 0x01 };
    zassert_equal(enc[0].payload - buf, sizeof(expected));
    zassert_mem_equal(buf, expected, sizeof(expected));
}

ZTEST(stow_protocol_serde, test_encode_error_with_index_only)
{
    uint8_t buf[16];
    zcbor_state_t enc[4];
    zcbor_new_encode_state(enc, ARRAY_SIZE(enc), buf, sizeof(buf), 1);

    struct stow_serde_error error = { .code = STOW_ERR_TOO_LARGE, .tag = 7, .item = STOW_SERDE_ABSENT, .index = 2 };
    zassert_equal(stow_wire_encode_error(enc, &error), 0);

    const uint8_t expected[] = { 0x85, 0x0B, 0x07, 0x09, 0xF6, 0x02 };
    zassert_equal(enc[0].payload - buf, sizeof(expected));
    zassert_mem_equal(buf, expected, sizeof(expected));
}

ZTEST(stow_protocol_serde, test_decode_missing_tag_is_malformed)
{
    const uint8_t request[] = { 0x81, 0x00 };
    zcbor_state_t dec[4];
    zcbor_new_decode_state(dec, ARRAY_SIZE(dec), request, sizeof(request), 1, NULL, 0);

    struct stow_serde_request req = { 0 };
    zassert_equal(stow_serde_decode_request(dec, &req), -EBADMSG);
    zassert_equal(req.tag, STOW_TAG_NONE);
}

ZTEST(stow_protocol_serde, test_decode_oversized_tag_is_malformed)
{
    const uint8_t request[] = { 0x82, 0x00, 0x1A, 0x00, 0x01, 0x00, 0x00 };
    zcbor_state_t dec[4];
    zcbor_new_decode_state(dec, ARRAY_SIZE(dec), request, sizeof(request), 1, NULL, 0);

    struct stow_serde_request req = { 0 };
    zassert_equal(stow_serde_decode_request(dec, &req), -EBADMSG);
    zassert_equal(req.tag, STOW_TAG_NONE);
}

ZTEST(stow_protocol_serde, test_decode_set_skips_the_values_of_unknown_items)
{
    uint8_t buf[32];
    int len = test_build_set_ints(buf, sizeof(buf), TEST_TAG, STOW_ID_TEST_INT, 1, STOW_ID_COUNT, 2);
    zcbor_state_t dec[4];
    zcbor_new_decode_state(dec, ARRAY_SIZE(dec), buf, len, 1, NULL, 0);

    struct stow_serde_request req = { 0 };
    zassert_equal(stow_serde_decode_request(dec, &req), 0);
    zassert_equal(req.entry_count, 2);

    zcbor_state_t entries[4];
    stow_serde_entries_start(&req, entries, ARRAY_SIZE(entries));
    uint32_t id = 0;
    zassert_equal(stow_serde_decode_entry(entries, &id), 0);
    data_value_t value = { 0 };
    zassert_equal(stow_decode(entries, STOW_ID_TEST_INT, &value), 0);
    zassert_equal(value.data.int_value, 1);
    stow_release(STOW_ID_TEST_INT, &value);
    zassert_equal(stow_serde_decode_entry(entries, &id), -ENOENT);
    zassert_equal(id, STOW_ID_COUNT);
}

ZTEST(stow_protocol_serde, test_decode_set_with_a_mistyped_value_names_the_item)
{
    uint8_t buf[32];
    zcbor_state_t enc[4];
    zcbor_new_encode_state(enc, ARRAY_SIZE(enc), buf, sizeof(buf), 1);
    zassert_true(zcbor_list_start_encode(enc, 6));
    zassert_true(zcbor_uint32_put(enc, STOW_MSG_SET));
    zassert_true(zcbor_uint32_put(enc, TEST_TAG));
    zassert_true(zcbor_uint32_put(enc, STOW_ID_TEST_INT));
    zassert_true(zcbor_int32_put(enc, 1));
    zassert_true(zcbor_uint32_put(enc, STOW_ID_TEST_INT));
    zassert_true(zcbor_tstr_put_lit(enc, "two"));
    zassert_true(zcbor_list_end_encode(enc, 6));

    zcbor_state_t dec[4];
    zcbor_new_decode_state(dec, ARRAY_SIZE(dec), buf, enc[0].payload - buf, 1, NULL, 0);

    struct stow_serde_request req = { 0 };
    zassert_equal(stow_serde_decode_request(dec, &req), -EBADMSG);
    zassert_equal(req.error_item, STOW_ID_TEST_INT);
}

ZTEST(stow_protocol_serde, test_encode_hello_response)
{
    uint8_t buf[96];
    zcbor_state_t enc[4];
    zcbor_new_encode_state(enc, ARRAY_SIZE(enc), buf, sizeof(buf), 1);

    struct stow_serde_hello hello = { .max_message = 300, .stow_hash = "abc", .roles = STOW_ROLE_GUEST };
    zassert_equal(stow_serde_encode_hello_response(enc, TEST_TAG, &hello), 0);

    zcbor_state_t dec[6];
    zcbor_new_decode_state(dec, ARRAY_SIZE(dec), buf, enc[0].payload - buf, 1, NULL, 0);
    uint32_t value = 0;
    zassert_true(zcbor_list_start_decode(dec));
    zassert_true(zcbor_uint32_decode(dec, &value));
    zassert_equal(value, STOW_MSG_HELLO_RESPONSE);
    zassert_true(zcbor_uint32_decode(dec, &value));
    zassert_equal(value, TEST_TAG);
    zassert_true(zcbor_uint32_decode(dec, &value));
    zassert_equal(value, STOW_PROTOCOL_VERSION);
    zassert_true(zcbor_map_start_decode(dec));
    zassert_true(zcbor_uint32_decode(dec, &value));
    zassert_equal(value, STOW_HELLO_MAX_MESSAGE);
    zassert_true(zcbor_uint32_decode(dec, &value));
    zassert_equal(value, 300);
}

ZTEST(stow_protocol_serde, test_encode_get_response_int)
{
    uint8_t buf[32];
    zcbor_state_t enc[4];
    zcbor_new_encode_state(enc, ARRAY_SIZE(enc), buf, sizeof(buf), 1);

    data_value_t value = { .type = STOW_ITEM_TYPE_INT, .data.int_value = -17 };
    zassert_equal(stow_serde_encode_get_start(enc, TEST_TAG, 1), 0);
    zassert_equal(stow_serde_encode_get_entry(enc, STOW_ID_TEST_INT, value), 0);

    const uint8_t expected[] = { 0x84, 0x05, 0x11, STOW_ID_TEST_INT, 0x30 };
    zassert_equal(enc[0].payload - buf, sizeof(expected));
    zassert_mem_equal(buf, expected, sizeof(expected));
}

ZTEST(stow_protocol_serde, test_encode_describe_response)
{
    const uint8_t chunk[] = { 0xA0 };  // CBOR empty map
    uint8_t buf[32];
    zcbor_state_t enc[4];
    zcbor_new_encode_state(enc, ARRAY_SIZE(enc), buf, sizeof(buf), 1);

    int ret = stow_serde_encode_describe_response(enc, TEST_TAG, STOW_SERDE_ABSENT, chunk, sizeof(chunk));
    zassert_equal(ret, 0);

    size_t encoded_len = enc[0].payload - buf;
    zcbor_state_t dec[4];
    zcbor_new_decode_state(dec, ARRAY_SIZE(dec), buf, encoded_len, 1, NULL, 0);
    zassert_true(zcbor_list_start_decode(dec));
    uint32_t cmd;
    zassert_true(zcbor_uint32_decode(dec, &cmd));
    zassert_equal(cmd, STOW_MSG_DESCRIBE_RESPONSE);
    uint32_t tag;
    zassert_true(zcbor_uint32_decode(dec, &tag));
    zassert_equal(tag, TEST_TAG);
    zassert_true(zcbor_nil_expect(dec, NULL));
    struct zcbor_string s;
    zassert_true(zcbor_bstr_decode(dec, &s));
    zassert_equal(s.len, sizeof(chunk));
    zassert_equal(s.value[0], chunk[0]);
    zassert_true(zcbor_list_end_decode(dec));
}

ZTEST(stow_protocol_serde, test_encode_update_int)
{
    uint8_t buf[32];
    zcbor_state_t enc[4];
    zcbor_new_encode_state(enc, ARRAY_SIZE(enc), buf, sizeof(buf), 1);

    data_value_t value = { .type = STOW_ITEM_TYPE_INT, .data.int_value = 5 };
    int ret = stow_serde_encode_update(enc, STOW_ID_TEST_INT, value);
    zassert_equal(ret, 0);

    size_t encoded_len = enc[0].payload - buf;
    zcbor_state_t dec[4];
    zcbor_new_decode_state(dec, ARRAY_SIZE(dec), buf, encoded_len, 1, NULL, 0);
    zassert_true(zcbor_list_start_decode(dec));
    uint32_t cmd;
    zassert_true(zcbor_uint32_decode(dec, &cmd));
    zassert_equal(cmd, STOW_MSG_UPDATE);
    zassert_true(zcbor_nil_expect(dec, NULL));
    uint32_t id;
    zassert_true(zcbor_uint32_decode(dec, &id));
    zassert_equal(id, STOW_ID_TEST_INT);
    int32_t v;
    zassert_true(zcbor_int32_decode(dec, &v));
    zassert_equal(v, 5);
    zassert_true(zcbor_list_end_decode(dec));
}

ZTEST(stow_protocol_serde, test_decode_get_request_with_several_items)
{
    uint8_t buf[32];
    zcbor_state_t enc[4];
    zcbor_new_encode_state(enc, ARRAY_SIZE(enc), buf, sizeof(buf), 1);
    zassert_true(zcbor_list_start_encode(enc, 4));
    zassert_true(zcbor_uint32_put(enc, STOW_MSG_GET));
    zassert_true(zcbor_uint32_put(enc, TEST_TAG));
    zassert_true(zcbor_uint32_put(enc, STOW_ID_TEST_INT));
    zassert_true(zcbor_uint32_put(enc, STOW_ID_TEST_STRING));
    zassert_true(zcbor_list_end_encode(enc, 4));
    size_t len = enc[0].payload - buf;

    zcbor_state_t dec[4];
    zcbor_new_decode_state(dec, ARRAY_SIZE(dec), buf, len, 1, NULL, 0);

    struct stow_serde_request req = { 0 };
    zassert_equal(stow_serde_decode_request(dec, &req), 0);
    zassert_equal(req.message_code, STOW_MSG_GET);
    zassert_equal(req.tag, TEST_TAG);
    zassert_equal(req.entry_count, 2);

    zcbor_state_t entries[4];
    stow_serde_entries_start(&req, entries, ARRAY_SIZE(entries));
    uint32_t id = 0;
    zassert_equal(stow_serde_decode_entry(entries, &id), 0);
    zassert_equal(id, STOW_ID_TEST_INT);
    zassert_equal(stow_serde_decode_entry(entries, &id), 0);
    zassert_equal(id, STOW_ID_TEST_STRING);
}

ZTEST(stow_protocol_serde, test_encode_get_response_with_several_items)
{
    uint8_t buf[64];
    zcbor_state_t enc[4];
    zcbor_new_encode_state(enc, ARRAY_SIZE(enc), buf, sizeof(buf), 1);

    data_value_t first = { .type = STOW_ITEM_TYPE_INT, .data.int_value = 3 };
    data_value_t second = { .type = STOW_ITEM_TYPE_INT, .data.int_value = 7 };
    zassert_equal(stow_serde_encode_get_start(enc, TEST_TAG, 2), 0);
    zassert_equal(stow_serde_encode_get_entry(enc, STOW_ID_TEST_INT, first), 0);
    zassert_equal(stow_serde_encode_get_entry(enc, STOW_ID_TEST_INT, second), 0);

    size_t encoded_len = enc[0].payload - buf;
    zcbor_state_t dec[4];
    zcbor_new_decode_state(dec, ARRAY_SIZE(dec), buf, encoded_len, 1, NULL, 0);
    zassert_true(zcbor_list_start_decode(dec));
    uint32_t cmd;
    zassert_true(zcbor_uint32_decode(dec, &cmd));
    zassert_equal(cmd, STOW_MSG_GET_RESPONSE);
    uint32_t tag;
    zassert_true(zcbor_uint32_decode(dec, &tag));
    zassert_equal(tag, TEST_TAG);
    uint32_t id0;
    zassert_true(zcbor_uint32_decode(dec, &id0));
    zassert_equal(id0, STOW_ID_TEST_INT);
    int32_t v0;
    zassert_true(zcbor_int32_decode(dec, &v0));
    zassert_equal(v0, 3);
    uint32_t id1;
    zassert_true(zcbor_uint32_decode(dec, &id1));
    zassert_equal(id1, STOW_ID_TEST_INT);
    int32_t v1;
    zassert_true(zcbor_int32_decode(dec, &v1));
    zassert_equal(v1, 7);
    zassert_true(zcbor_list_end_decode(dec));
}

ZTEST(stow_protocol_serde, test_decode_subscribe_without_items_is_malformed)
{
    uint8_t buf[8];
    int len = test_build_simple(buf, sizeof(buf), STOW_MSG_SUBSCRIBE, TEST_TAG);
    zcbor_state_t dec[4];
    zcbor_new_decode_state(dec, ARRAY_SIZE(dec), buf, len, 1, NULL, 0);

    struct stow_serde_request req = { 0 };
    zassert_equal(stow_serde_decode_request(dec, &req), -EBADMSG);
    zassert_equal(req.tag, TEST_TAG);
}
