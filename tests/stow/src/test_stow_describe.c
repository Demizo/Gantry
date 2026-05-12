/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "test_common.h"

ZTEST_SUITE(stow_describe, NULL, NULL, reset_stow, NULL, NULL);

ZTEST(stow_describe, test_full_describe_succeeds)
{
    uint8_t buf[4096];
    zcbor_state_t enc[4];
    zcbor_new_encode_state(enc, ARRAY_SIZE(enc), buf, sizeof(buf), STOW_ID_COUNT);

    struct stow_describe_state state;
    stow_describe_start(&state);

    int ret = stow_describe(&state, enc);
    zassert_equal(ret, 0);
    zassert_equal(state.current_id, STOW_ID_COUNT);
}

ZTEST(stow_describe, test_tiny_buffer_returns_enomem)
{
    uint8_t buf[1];
    zcbor_state_t enc[4];
    zcbor_new_encode_state(enc, ARRAY_SIZE(enc), buf, sizeof(buf), STOW_ID_COUNT);

    struct stow_describe_state state;
    stow_describe_start(&state);

    int ret = stow_describe(&state, enc);
    zassert_equal(ret, -ENOMEM);
    zassert_equal(state.current_id, 0);
}

ZTEST(stow_describe, test_rollback_leaves_no_partial_data)
{
    uint8_t buf[8];
    zcbor_state_t enc[4];
    zcbor_new_encode_state(enc, ARRAY_SIZE(enc), buf, sizeof(buf), STOW_ID_COUNT);

    const uint8_t* payload_before = enc[0].payload;

    struct stow_describe_state state;
    stow_describe_start(&state);
    stow_describe(&state, enc);

    zassert_equal_ptr(enc[0].payload, payload_before);
}

ZTEST(stow_describe, test_chunked_describe_covers_all_items)
{
    struct stow_describe_state state;
    stow_describe_start(&state);

    uint32_t items_encoded = 0;

    while (state.current_id < STOW_ID_COUNT)
    {
        uint8_t buf[1024];
        zcbor_state_t enc[4];
        zcbor_new_encode_state(enc, ARRAY_SIZE(enc), buf, sizeof(buf), STOW_ID_COUNT);

        uint32_t id_before = state.current_id;
        int ret = stow_describe(&state, enc);

        if (ret == 0)
        {
            items_encoded += STOW_ID_COUNT - id_before;
            break;
        }
        else if (ret == -ENOMEM)
        {
            uint32_t newly_encoded_count = state.current_id - id_before;
            zassert_true(newly_encoded_count > 0);
            items_encoded += newly_encoded_count;
        }
        else
        {
            zassert_unreachable("Unexpected return value from stow_describe");
        }
    }

    zassert_equal(items_encoded, STOW_ID_COUNT);
}

#define EXPECT_KEY(dec_state, expected_str)                                                                          \
    do                                                                                                               \
    {                                                                                                                \
        struct zcbor_string key;                                                                                     \
        zassert_true(zcbor_tstr_decode(dec_state, &key), "Failed to decode key");                                    \
        zassert_equal(key.len, strlen(expected_str), "Key length mismatch for %s", expected_str);                    \
        zassert_mem_equal(key.value, expected_str, strlen(expected_str), "Key mismatch, expected %s", expected_str); \
    } while (0)

ZTEST(stow_describe, test_describe_encoding)
{
    uint8_t buf[256];
    zcbor_state_t enc[4];
    zcbor_new_encode_state(enc, ARRAY_SIZE(enc), buf, sizeof(buf), STOW_ID_COUNT);

    struct stow_describe_state state;
    stow_describe_start(&state);

    int ret = stow_describe(&state, enc);
    zassert_equal(ret, -ENOMEM);
    zassert_true(state.current_id > 0);

    ZCBOR_STATE_D(dec, 1, buf, enc->payload - buf, 1, 0);
    uint32_t val_u32;
    struct zcbor_string val_tstr;

    // Start map decode
    zassert_true(zcbor_map_start_decode(dec), "Failed to start map decode");

    // id
    EXPECT_KEY(dec, "id");
    zassert_true(zcbor_uint32_decode(dec, &val_u32), "Failed to decode id");

    // name
    EXPECT_KEY(dec, "name");
    zassert_true(zcbor_tstr_decode(dec, &val_tstr), "Failed to decode name");

    // categories (Skip the value)
    EXPECT_KEY(dec, "categories");
    zassert_true(zcbor_any_skip(dec, NULL), "Failed to skip 'categories' value");

    // storage
    EXPECT_KEY(dec, "storage");
    zassert_true(zcbor_tstr_decode(dec, &val_tstr), "Failed to decode storage");

    // read_perm
    EXPECT_KEY(dec, "read_perm");
    zassert_true(zcbor_tstr_decode(dec, &val_tstr), "Failed to decode read_perm");

    // write_perm
    EXPECT_KEY(dec, "write_perm");
    zassert_true(zcbor_tstr_decode(dec, &val_tstr), "Failed to decode write_perm");

    // type
    EXPECT_KEY(dec, "type");
    zassert_true(zcbor_tstr_decode(dec, &val_tstr), "Failed to decode type");

    // default (Skip the value)
    EXPECT_KEY(dec, "default");
    zassert_true(zcbor_any_skip(dec, NULL), "Failed to skip 'default' value");

    // constraints (Skip the value)
    EXPECT_KEY(dec, "constraints");
    zassert_true(zcbor_any_skip(dec, NULL), "Failed to skip 'constraints' value");

    // End map decode
    zassert_true(zcbor_map_end_decode(dec), "Failed to end map decode");
}
