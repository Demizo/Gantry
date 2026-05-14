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

ZTEST(stow_protocol_serde, test_decode_version_request)
{
    uint8_t buf[8];
    int len = test_build_simple(buf, sizeof(buf), STOW_MSG_VERSION);
    zassert_true(len > 0);

    zcbor_state_t dec[4];
    zcbor_new_decode_state(dec, ARRAY_SIZE(dec), buf, len, 1, NULL, 0);

    struct stow_serde_request req = { 0 };
    int ret = stow_serde_decode_request(dec, &req);
    zassert_equal(ret, 0);
    zassert_equal(req.message_code, STOW_MSG_VERSION);
    zassert_false(req.has_value);

    stow_serde_release_request(&req);
}

ZTEST(stow_protocol_serde, test_decode_describe_request)
{
    uint8_t buf[8];
    int len = test_build_with_id(buf, sizeof(buf), STOW_MSG_DESCRIBE, 0);
    zcbor_state_t dec[4];
    zcbor_new_decode_state(dec, ARRAY_SIZE(dec), buf, len, 1, NULL, 0);

    struct stow_serde_request req = { 0 };
    zassert_equal(stow_serde_decode_request(dec, &req), 0);
    zassert_equal(req.message_code, STOW_MSG_DESCRIBE);
    stow_serde_release_request(&req);
}

ZTEST(stow_protocol_serde, test_decode_get_request)
{
    uint8_t buf[16];
    int len = test_build_with_id(buf, sizeof(buf), STOW_MSG_GET, STOW_ID_TEST_INT);
    zcbor_state_t dec[4];
    zcbor_new_decode_state(dec, ARRAY_SIZE(dec), buf, len, 1, NULL, 0);

    struct stow_serde_request req = { 0 };
    zassert_equal(stow_serde_decode_request(dec, &req), 0);
    zassert_equal(req.message_code, STOW_MSG_GET);
    zassert_equal(req.item_id, STOW_ID_TEST_INT);
    zassert_false(req.has_value);
    stow_serde_release_request(&req);
}

ZTEST(stow_protocol_serde, test_decode_set_request_int)
{
    uint8_t buf[16];
    int len = test_build_set_int(buf, sizeof(buf), STOW_ID_TEST_INT, 42);
    zcbor_state_t dec[4];
    zcbor_new_decode_state(dec, ARRAY_SIZE(dec), buf, len, 1, NULL, 0);

    struct stow_serde_request req = { 0 };
    zassert_equal(stow_serde_decode_request(dec, &req), 0);
    zassert_equal(req.message_code, STOW_MSG_SET);
    zassert_equal(req.item_id, STOW_ID_TEST_INT);
    zassert_true(req.has_value);
    zassert_equal(req.value.type, STOW_ITEM_TYPE_INT);
    zassert_equal(req.value.data.int_value, 42);
    stow_serde_release_request(&req);
}

ZTEST(stow_protocol_serde, test_decode_set_request_string)
{
    uint8_t buf[32];
    int len = test_build_set_string(buf, sizeof(buf), STOW_ID_TEST_STRING, "world");
    zcbor_state_t dec[4];
    zcbor_new_decode_state(dec, ARRAY_SIZE(dec), buf, len, 1, NULL, 0);

    struct stow_serde_request req = { 0 };
    zassert_equal(stow_serde_decode_request(dec, &req), 0);
    zassert_equal(req.message_code, STOW_MSG_SET);
    zassert_equal(req.item_id, STOW_ID_TEST_STRING);
    zassert_true(req.has_value);
    zassert_equal(req.value.type, STOW_ITEM_TYPE_STRING);
    zassert_str_equal(req.value.data.string_value, "world");
    stow_serde_release_request(&req);
}

ZTEST(stow_protocol_serde, test_decode_subscribe_request)
{
    uint8_t buf[16];
    int len = test_build_with_id(buf, sizeof(buf), STOW_MSG_SUBSCRIBE, STOW_ID_TEST_INT);
    zcbor_state_t dec[4];
    zcbor_new_decode_state(dec, ARRAY_SIZE(dec), buf, len, 1, NULL, 0);

    struct stow_serde_request req = { 0 };
    zassert_equal(stow_serde_decode_request(dec, &req), 0);
    zassert_equal(req.message_code, STOW_MSG_SUBSCRIBE);
    zassert_equal(req.item_id, STOW_ID_TEST_INT);
    stow_serde_release_request(&req);
}

ZTEST(stow_protocol_serde, test_decode_unknown_command_returns_enotsup)
{
    uint8_t buf[8];
    int len = test_build_simple(buf, sizeof(buf), 99);
    zcbor_state_t dec[4];
    zcbor_new_decode_state(dec, ARRAY_SIZE(dec), buf, len, 1, NULL, 0);

    struct stow_serde_request req = { 0 };
    int ret = stow_serde_decode_request(dec, &req);
    zassert_equal(ret, -ENOTSUP);
    stow_serde_release_request(&req);
}

ZTEST(stow_protocol_serde, test_decode_invalid_item_id_returns_einval)
{
    uint8_t buf[16];
    int len = test_build_with_id(buf, sizeof(buf), STOW_MSG_GET, STOW_ID_COUNT + 5);
    zcbor_state_t dec[4];
    zcbor_new_decode_state(dec, ARRAY_SIZE(dec), buf, len, 1, NULL, 0);

    struct stow_serde_request req = { 0 };
    int ret = stow_serde_decode_request(dec, &req);
    zassert_equal(ret, -EINVAL);
    stow_serde_release_request(&req);
}

ZTEST(stow_protocol_serde, test_decode_malformed_returns_ebadmsg)
{
    uint8_t bogus[] = { 0xFF, 0xFF, 0xFF };
    zcbor_state_t dec[4];
    zcbor_new_decode_state(dec, ARRAY_SIZE(dec), bogus, sizeof(bogus), 1, NULL, 0);

    struct stow_serde_request req = { 0 };
    int ret = stow_serde_decode_request(dec, &req);
    zassert_equal(ret, -EBADMSG);
    stow_serde_release_request(&req);
}

//**********************************************************
//* Encode tests
//**********************************************************

ZTEST(stow_protocol_serde, test_encode_ok)
{
    uint8_t buf[16];
    zcbor_state_t enc[4];
    zcbor_new_encode_state(enc, ARRAY_SIZE(enc), buf, sizeof(buf), 1);

    int ret = stow_serde_encode_ok(enc);
    zassert_equal(ret, 0);

    size_t encoded_len = enc[0].payload - buf;
    zcbor_state_t dec[4];
    zcbor_new_decode_state(dec, ARRAY_SIZE(dec), buf, encoded_len, 1, NULL, 0);
    zassert_true(zcbor_list_start_decode(dec));
    uint32_t cmd;
    zassert_true(zcbor_uint32_decode(dec, &cmd));
    zassert_equal(cmd, STOW_MSG_OK);
    zassert_true(zcbor_list_end_decode(dec));
}

ZTEST(stow_protocol_serde, test_encode_error)
{
    uint8_t buf[16];
    zcbor_state_t enc[4];
    zcbor_new_encode_state(enc, ARRAY_SIZE(enc), buf, sizeof(buf), 1);

    int ret = stow_serde_encode_error(enc, STOW_ERR_PERMISSION_DENIED);
    zassert_equal(ret, 0);

    size_t encoded_len = enc[0].payload - buf;
    zcbor_state_t dec[4];
    zcbor_new_decode_state(dec, ARRAY_SIZE(dec), buf, encoded_len, 1, NULL, 0);
    zassert_true(zcbor_list_start_decode(dec));
    uint32_t cmd;
    zassert_true(zcbor_uint32_decode(dec, &cmd));
    zassert_equal(cmd, STOW_MSG_ERROR);
    uint32_t code;
    zassert_true(zcbor_uint32_decode(dec, &code));
    zassert_equal(code, STOW_ERR_PERMISSION_DENIED);
    zassert_true(zcbor_list_end_decode(dec));
}

ZTEST(stow_protocol_serde, test_encode_version_response)
{
    uint8_t buf[16];
    zcbor_state_t enc[4];
    zcbor_new_encode_state(enc, ARRAY_SIZE(enc), buf, sizeof(buf), 1);

    int ret = stow_serde_encode_version_response(enc);
    zassert_equal(ret, 0);

    size_t encoded_len = enc[0].payload - buf;
    zcbor_state_t dec[4];
    zcbor_new_decode_state(dec, ARRAY_SIZE(dec), buf, encoded_len, 1, NULL, 0);
    zassert_true(zcbor_list_start_decode(dec));
    uint32_t cmd;
    zassert_true(zcbor_uint32_decode(dec, &cmd));
    zassert_equal(cmd, STOW_MSG_VERSION_RESPONSE);
    uint32_t version;
    zassert_true(zcbor_uint32_decode(dec, &version));
    zassert_equal(version, STOW_PROTOCOL_VERSION);
    zassert_true(zcbor_list_end_decode(dec));
}

ZTEST(stow_protocol_serde, test_encode_get_response_int)
{
    uint8_t buf[32];
    zcbor_state_t enc[4];
    zcbor_new_encode_state(enc, ARRAY_SIZE(enc), buf, sizeof(buf), 1);

    data_value_t value = { .type = STOW_ITEM_TYPE_INT, .data.int_value = -17 };
    int ret = stow_serde_encode_get_response(enc, STOW_ID_TEST_INT, value);
    zassert_equal(ret, 0);

    size_t encoded_len = enc[0].payload - buf;
    zcbor_state_t dec[4];
    zcbor_new_decode_state(dec, ARRAY_SIZE(dec), buf, encoded_len, 1, NULL, 0);
    zassert_true(zcbor_list_start_decode(dec));
    uint32_t cmd;
    zassert_true(zcbor_uint32_decode(dec, &cmd));
    zassert_equal(cmd, STOW_MSG_GET_RESPONSE);
    uint32_t id;
    zassert_true(zcbor_uint32_decode(dec, &id));
    zassert_equal(id, STOW_ID_TEST_INT);
    int32_t v;
    zassert_true(zcbor_int32_decode(dec, &v));
    zassert_equal(v, -17);
    zassert_true(zcbor_list_end_decode(dec));
}

ZTEST(stow_protocol_serde, test_encode_describe_response)
{
    const uint8_t chunk[] = { 0xA0 };  // CBOR empty map
    uint8_t buf[32];
    zcbor_state_t enc[4];
    zcbor_new_encode_state(enc, ARRAY_SIZE(enc), buf, sizeof(buf), 1);

    int ret = stow_serde_encode_describe_response(enc, 0, false, chunk, sizeof(chunk));
    zassert_equal(ret, 0);

    size_t encoded_len = enc[0].payload - buf;
    zcbor_state_t dec[4];
    zcbor_new_decode_state(dec, ARRAY_SIZE(dec), buf, encoded_len, 1, NULL, 0);
    zassert_true(zcbor_list_start_decode(dec));
    uint32_t cmd;
    zassert_true(zcbor_uint32_decode(dec, &cmd));
    zassert_equal(cmd, STOW_MSG_DESCRIBE_RESPONSE);
    uint32_t next_id;
    zassert_true(zcbor_uint32_decode(dec, &next_id));
    zassert_equal(next_id, 0);
    bool has_more;
    zassert_true(zcbor_bool_decode(dec, &has_more));
    zassert_equal(has_more, false);
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
    uint32_t id;
    zassert_true(zcbor_uint32_decode(dec, &id));
    zassert_equal(id, STOW_ID_TEST_INT);
    int32_t v;
    zassert_true(zcbor_int32_decode(dec, &v));
    zassert_equal(v, 5);
    zassert_true(zcbor_list_end_decode(dec));
}

ZTEST(stow_protocol_serde, test_decode_multi_get_request)
{
    uint8_t buf[32];
    zcbor_state_t enc[4];
    zcbor_new_encode_state(enc, ARRAY_SIZE(enc), buf, sizeof(buf), 1);
    zassert_true(zcbor_list_start_encode(enc, 3));
    zassert_true(zcbor_uint32_put(enc, STOW_MSG_MULTI_GET));
    zassert_true(zcbor_uint32_put(enc, STOW_ID_TEST_INT));
    zassert_true(zcbor_uint32_put(enc, STOW_ID_TEST_STRING));
    zassert_true(zcbor_list_end_encode(enc, 3));
    size_t len = enc[0].payload - buf;

    zcbor_state_t dec[4];
    zcbor_new_decode_state(dec, ARRAY_SIZE(dec), buf, len, 1, NULL, 0);

    struct stow_serde_request req = { 0 };
    zassert_equal(stow_serde_decode_request(dec, &req), 0);
    zassert_equal(req.message_code, STOW_MSG_MULTI_GET);
    zassert_equal(req.multi_count, 2);
    zassert_equal(req.multi_ids[0], STOW_ID_TEST_INT);
    zassert_equal(req.multi_ids[1], STOW_ID_TEST_STRING);
    stow_serde_release_request(&req);
}

ZTEST(stow_protocol_serde, test_decode_multi_set_request)
{
    uint8_t buf[32];
    zcbor_state_t enc[4];
    zcbor_new_encode_state(enc, ARRAY_SIZE(enc), buf, sizeof(buf), 1);
    zassert_true(zcbor_list_start_encode(enc, 3));
    zassert_true(zcbor_uint32_put(enc, STOW_MSG_MULTI_SET));
    zassert_true(zcbor_uint32_put(enc, STOW_ID_TEST_INT));
    zassert_true(zcbor_int32_put(enc, 77));
    zassert_true(zcbor_list_end_encode(enc, 3));
    size_t len = enc[0].payload - buf;

    zcbor_state_t dec[4];
    zcbor_new_decode_state(dec, ARRAY_SIZE(dec), buf, len, 1, NULL, 0);

    struct stow_serde_request req = { 0 };
    zassert_equal(stow_serde_decode_request(dec, &req), 0);
    zassert_equal(req.message_code, STOW_MSG_MULTI_SET);
    zassert_equal(req.multi_count, 1);
    zassert_equal(req.multi_ids[0], STOW_ID_TEST_INT);
    zassert_true(req.multi_has_value[0]);
    zassert_equal(req.multi_values[0].type, STOW_ITEM_TYPE_INT);
    zassert_equal(req.multi_values[0].data.int_value, 77);
    stow_serde_release_request(&req);
}

ZTEST(stow_protocol_serde, test_encode_multi_get_response)
{
    uint8_t buf[64];
    zcbor_state_t enc[4];
    zcbor_new_encode_state(enc, ARRAY_SIZE(enc), buf, sizeof(buf), 1);

    enum stow_item_id ids[] = { STOW_ID_TEST_INT, STOW_ID_TEST_INT };
    data_value_t values[] = {
        { .type = STOW_ITEM_TYPE_INT, .data.int_value = 3 },
        { .type = STOW_ITEM_TYPE_INT, .data.int_value = 7 },
    };
    int ret = stow_serde_encode_multi_get_response(enc, ids, values, 2);
    zassert_equal(ret, 0);

    size_t encoded_len = enc[0].payload - buf;
    zcbor_state_t dec[4];
    zcbor_new_decode_state(dec, ARRAY_SIZE(dec), buf, encoded_len, 1, NULL, 0);
    zassert_true(zcbor_list_start_decode(dec));
    uint32_t cmd;
    zassert_true(zcbor_uint32_decode(dec, &cmd));
    zassert_equal(cmd, STOW_MSG_MULTI_GET_RESPONSE);
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
