/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "test_common.h"

#define SESSION_ID 101

/**
 * @brief Decoded Hello Response
 */
struct hello
{
    uint32_t version;
    uint32_t max_message;
    char stow_hash[STOW_HASH_MAX_LEN + 1];
    uint32_t roles;
    char role_names[STOW_ROLE_COUNT][16];
    size_t role_name_count;
};

static void before_each(void* fixture)
{
    (void)fixture;
    test_reset();
}

static void after_each(void* fixture)
{
    (void)fixture;
    test_endpoint_close(SESSION_ID);
    test_sync();
    test_drain_captures();
}

ZTEST_SUITE(stow_protocol_hello, NULL, NULL, before_each, after_each, NULL);

static void copy_tstr(char* dest, size_t size, const struct zcbor_string* str)
{
    zassert_true(str->len < size);
    memcpy(dest, str->value, str->len);
    dest[str->len] = '\0';
}

static struct hello request_hello(stow_role_t roles)
{
    zassert_equal(test_endpoint_open(SESSION_ID, roles, &test_transport), 0);

    uint8_t req[8];
    int len = test_build_simple(req, sizeof(req), STOW_MSG_HELLO, TEST_TAG);
    zassert_equal(test_submit_rx(SESSION_ID, req, len), 0);

    struct test_capture cap = test_await_tx(K_SECONDS(1));
    zassert_not_null(cap.buf);

    uint32_t cmd = 0;
    uint32_t tag = 0;
    zcbor_state_t dec[8];
    zassert_true(test_decode_response_header(cap.buf, &cmd, &tag, dec, ARRAY_SIZE(dec)));
    zassert_equal(cmd, STOW_MSG_HELLO_RESPONSE);
    zassert_equal(tag, TEST_TAG);

    struct hello hello = { 0 };
    zassert_true(zcbor_uint32_decode(dec, &hello.version));
    zassert_true(zcbor_map_start_decode(dec));
    while (!zcbor_array_at_end(dec))
    {
        uint32_t key = 0;
        zassert_true(zcbor_uint32_decode(dec, &key));
        struct zcbor_string str;
        switch (key)
        {
            case STOW_HELLO_MAX_MESSAGE:
                zassert_true(zcbor_uint32_decode(dec, &hello.max_message));
                break;
            case STOW_HELLO_STOW_HASH:
                zassert_true(zcbor_tstr_decode(dec, &str));
                copy_tstr(hello.stow_hash, sizeof(hello.stow_hash), &str);
                break;
            case STOW_HELLO_ROLES:
                zassert_true(zcbor_uint32_decode(dec, &hello.roles));
                break;
            case STOW_HELLO_ROLE_NAMES:
                zassert_true(zcbor_list_start_decode(dec));
                while (!zcbor_array_at_end(dec))
                {
                    zassert_true(hello.role_name_count < ARRAY_SIZE(hello.role_names));
                    zassert_true(zcbor_tstr_decode(dec, &str));
                    copy_tstr(hello.role_names[hello.role_name_count], sizeof(hello.role_names[0]), &str);
                    hello.role_name_count++;
                }
                zassert_true(zcbor_list_end_decode(dec));
                break;
            default:
                zassert_unreachable("unexpected Hello key %u", key);
        }
    }
    zassert_true(zcbor_map_end_decode(dec));
    zassert_true(zcbor_list_end_decode(dec));

    net_buf_unref(cap.buf);
    return hello;
}

ZTEST(stow_protocol_hello, test_hello_reports_the_version_and_limits)
{
    struct hello hello = request_hello(STOW_ROLE_GUEST);

    zassert_equal(hello.version, STOW_PROTOCOL_VERSION);
    zassert_equal(hello.max_message, TEST_BUF_DATA_SIZE - TEST_TX_HEADROOM - TEST_TX_TAILROOM);
}

ZTEST(stow_protocol_hello, test_hello_reports_stow_hash)
{
    struct hello hello = request_hello(STOW_ROLE_GUEST);

    data_value_t hash = { 0 };
    zassert_equal(stow_get(STOW_ID_STOW_HASH, &hash), 0);
    zassert_str_equal(hello.stow_hash, hash.data.string_value);
    stow_release(STOW_ID_STOW_HASH, &hash);
}

ZTEST(stow_protocol_hello, test_hello_reports_the_session_roles)
{
    struct hello hello = request_hello(STOW_ROLE_SESSION | STOW_ROLE_DEV);

    zassert_equal(hello.roles, STOW_ROLE_SESSION | STOW_ROLE_DEV);
}

ZTEST(stow_protocol_hello, test_hello_names_every_role_by_bit)
{
    struct hello hello = request_hello(0);

    zassert_equal(hello.roles, 0);
    zassert_equal(hello.role_name_count, STOW_ROLE_COUNT);
    for (size_t bit = 0; bit < STOW_ROLE_COUNT; bit++)
    {
        zassert_str_equal(hello.role_names[bit], g_stow_role_names[bit]);
    }
    zassert_str_equal(hello.role_names[0], "Guest");
}
