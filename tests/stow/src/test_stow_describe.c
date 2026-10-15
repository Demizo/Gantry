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

    uint32_t next_id = 0;
    int ret = stow_describe(0, NULL, enc, &next_id);
    zassert_equal(ret, 0);
    zassert_equal(next_id, STOW_ID_COUNT);
}

ZTEST(stow_describe, test_tiny_buffer_returns_e2big)
{
    uint8_t buf[1];
    zcbor_state_t enc[4];
    zcbor_new_encode_state(enc, ARRAY_SIZE(enc), buf, sizeof(buf), STOW_ID_COUNT);

    uint32_t next_id = STOW_ID_COUNT;
    int ret = stow_describe(0, NULL, enc, &next_id);
    zassert_equal(ret, -E2BIG);
    zassert_equal(next_id, 0);
    zassert_equal_ptr(enc[0].payload, buf);
}

ZTEST(stow_describe, test_filter_describes_only_selected_items)
{
    ATOMIC_DEFINE(filter, STOW_ID_COUNT) = { 0 };
    atomic_set_bit(filter, STOW_ID_TEST_INT);

    uint8_t buf[512];
    zcbor_state_t enc[4];
    zcbor_new_encode_state(enc, ARRAY_SIZE(enc), buf, sizeof(buf), STOW_ID_COUNT);

    uint32_t next_id = 0;
    zassert_equal(stow_describe(0, filter, enc, &next_id), 0);
    zassert_equal(next_id, STOW_ID_COUNT);

    ZCBOR_STATE_D(dec, 3, buf, enc[0].payload - buf, 1, 0);
    zassert_true(zcbor_map_start_decode(dec));
    uint32_t key;
    uint32_t id;
    zassert_true(zcbor_uint32_decode(dec, &key));
    zassert_equal(key, STOW_DESCRIBE_ID);
    zassert_true(zcbor_uint32_decode(dec, &id));
    zassert_equal(id, STOW_ID_TEST_INT);
    for (int i = 0; i < 8 * 2; i++)
    {
        zassert_true(zcbor_any_skip(dec, NULL));
    }
    zassert_true(zcbor_map_end_decode(dec));
    zassert_equal_ptr(dec->payload, enc[0].payload);
}

ZTEST(stow_describe, test_empty_filter_describes_nothing)
{
    ATOMIC_DEFINE(filter, STOW_ID_COUNT) = { 0 };

    uint8_t buf[8];
    zcbor_state_t enc[4];
    zcbor_new_encode_state(enc, ARRAY_SIZE(enc), buf, sizeof(buf), STOW_ID_COUNT);

    uint32_t next_id = 0;
    zassert_equal(stow_describe(0, filter, enc, &next_id), 0);
    zassert_equal(next_id, STOW_ID_COUNT);

    zassert_equal_ptr(enc[0].payload, buf);
}

ZTEST(stow_describe, test_rollback_leaves_no_partial_data)
{
    uint8_t buf[8];
    zcbor_state_t enc[4];
    zcbor_new_encode_state(enc, ARRAY_SIZE(enc), buf, sizeof(buf), STOW_ID_COUNT);

    const uint8_t* payload_before = enc[0].payload;

    uint32_t next_id = 0;
    stow_describe(0, NULL, enc, &next_id);

    zassert_equal_ptr(enc[0].payload, payload_before);
}

ZTEST(stow_describe, test_chunked_describe_covers_all_items)
{
    uint32_t next_id = 0;
    uint32_t items_encoded = 0;

    while (next_id < STOW_ID_COUNT)
    {
        uint8_t buf[1024];
        zcbor_state_t enc[4];
        zcbor_new_encode_state(enc, ARRAY_SIZE(enc), buf, sizeof(buf), STOW_ID_COUNT);

        uint32_t id_before = next_id;
        int ret = stow_describe(next_id, NULL, enc, &next_id);

        if (ret == 0)
        {
            items_encoded += STOW_ID_COUNT - id_before;
            break;
        }
        else if (ret == -ENOMEM)
        {
            uint32_t newly_encoded_count = next_id - id_before;
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

#define EXPECT_KEY(dec_state, expected_key)                                                 \
    do                                                                                      \
    {                                                                                       \
        uint32_t key;                                                                       \
        zassert_true(zcbor_uint32_decode(dec_state, &key), "Failed to decode key");         \
        zassert_equal(key, (expected_key), "Expected key %u, got %u", (expected_key), key); \
    } while (0)

ZTEST(stow_describe, test_describe_encoding)
{
    uint8_t buf[256];
    zcbor_state_t enc[4];
    zcbor_new_encode_state(enc, ARRAY_SIZE(enc), buf, sizeof(buf), STOW_ID_COUNT);

    uint32_t next_id = 0;
    int ret = stow_describe(0, NULL, enc, &next_id);
    zassert_equal(ret, -ENOMEM);
    zassert_true(next_id > 0);

    ZCBOR_STATE_D(dec, 2, buf, enc->payload - buf, 1, 0);
    uint32_t val_u32;
    struct zcbor_string val_tstr;

    // Start map decode
    zassert_true(zcbor_map_start_decode(dec), "Failed to start map decode");

    const struct stow_item_const_metadata* item = &g_stow_const_metadata[0];

    // id
    EXPECT_KEY(dec, STOW_DESCRIBE_ID);
    zassert_true(zcbor_uint32_decode(dec, &val_u32), "Failed to decode id");
    zassert_equal(val_u32, 0);

    // name
    EXPECT_KEY(dec, STOW_DESCRIBE_NAME);
    zassert_true(zcbor_tstr_decode(dec, &val_tstr), "Failed to decode name");

    // categories (Skip the value)
    EXPECT_KEY(dec, STOW_DESCRIBE_CATEGORIES);
    zassert_true(zcbor_any_skip(dec, NULL), "Failed to skip 'categories' value");

    // storage
    EXPECT_KEY(dec, STOW_DESCRIBE_STORAGE);
    zassert_true(zcbor_uint32_decode(dec, &val_u32), "Failed to decode storage");
    zassert_equal(val_u32, item->storage_type);

    // read_perm
    EXPECT_KEY(dec, STOW_DESCRIBE_READ_PERM);
    zassert_true(zcbor_uint32_decode(dec, &val_u32), "Failed to decode read_perm");
    zassert_equal(val_u32, item->permissions.read_permissions);

    // write_perm
    EXPECT_KEY(dec, STOW_DESCRIBE_WRITE_PERM);
    zassert_true(zcbor_uint32_decode(dec, &val_u32), "Failed to decode write_perm");
    zassert_equal(val_u32, item->permissions.write_permissions);

    // type
    EXPECT_KEY(dec, STOW_DESCRIBE_TYPE);
    zassert_true(zcbor_uint32_decode(dec, &val_u32), "Failed to decode type");
    zassert_equal(val_u32, item->type);

    // default (Skip the value)
    EXPECT_KEY(dec, STOW_DESCRIBE_DEFAULT);
    zassert_true(zcbor_any_skip(dec, NULL), "Failed to skip 'default' value");

    // constraints (Skip the value)
    EXPECT_KEY(dec, STOW_DESCRIBE_CONSTRAINTS);
    zassert_true(zcbor_any_skip(dec, NULL), "Failed to skip 'constraints' value");

    // End map decode
    zassert_true(zcbor_map_end_decode(dec), "Failed to end map decode");
}

/**
 * @brief Describe one item and position a decoder at its constraints
 *
 * @return Encoded length
 */
static size_t describe_to_constraints(uint32_t id, uint8_t* buf, size_t size, zcbor_state_t* dec, size_t dec_states)
{
    ATOMIC_DEFINE(filter, STOW_ID_COUNT) = { 0 };
    atomic_set_bit(filter, (int)id);
    zcbor_state_t enc[4];
    zcbor_new_encode_state(enc, ARRAY_SIZE(enc), buf, size, STOW_ID_COUNT);
    uint32_t next_id = 0;
    zassert_equal(stow_describe(0, filter, enc, &next_id), 0);
    size_t len = enc[0].payload - buf;

    zcbor_new_decode_state(dec, dec_states, buf, len, 1, NULL, 0);
    zassert_true(zcbor_map_start_decode(dec));
    for (uint32_t key = STOW_DESCRIBE_ID; key < STOW_DESCRIBE_CONSTRAINTS; key++)
    {
        EXPECT_KEY(dec, key);
        zassert_true(zcbor_any_skip(dec, NULL));
    }
    EXPECT_KEY(dec, STOW_DESCRIBE_CONSTRAINTS);
    return len;
}

ZTEST(stow_describe, test_float_constraints_use_integer_keys)
{
    uint8_t buf[256];
    zcbor_state_t dec[6];
    describe_to_constraints(STOW_ID_TEST_FLOAT, buf, sizeof(buf), dec, ARRAY_SIZE(dec));
    const struct stow_float_constraints* expected =
        &g_stow_const_metadata[STOW_ID_TEST_FLOAT].constraints.float_constraints;

    float min = 0;
    float max = 0;
    zassert_true(zcbor_map_start_decode(dec));
    EXPECT_KEY(dec, STOW_CONSTRAINT_MIN);
    zassert_true(zcbor_float32_decode(dec, &min));
    EXPECT_KEY(dec, STOW_CONSTRAINT_MAX);
    zassert_true(zcbor_float32_decode(dec, &max));
    zassert_true(zcbor_map_end_decode(dec));
    zassert_equal(min, expected->min);
    zassert_equal(max, expected->max);
}

ZTEST(stow_describe, test_enum_constraints_use_integer_keys)
{
    uint8_t buf[256];
    zcbor_state_t dec[6];
    describe_to_constraints(STOW_ID_TEST_ENUM, buf, sizeof(buf), dec, ARRAY_SIZE(dec));

    int32_t value = -1;
    struct zcbor_string name;
    zassert_true(zcbor_list_start_decode(dec));
    zassert_true(zcbor_map_start_decode(dec));
    EXPECT_KEY(dec, STOW_ENUM_CONSTRAINT_VALUE);
    zassert_true(zcbor_int32_decode(dec, &value));
    EXPECT_KEY(dec, STOW_ENUM_CONSTRAINT_NAME);
    zassert_true(zcbor_tstr_decode(dec, &name));
    zassert_true(zcbor_map_end_decode(dec));
    zassert_equal(value, 0);
    zassert_equal(name.len, strlen("RED"));
    zassert_mem_equal(name.value, "RED", name.len);
}

ZTEST(stow_describe, test_struct_field_constraints_use_integer_keys)
{
    uint8_t buf[1024];
    zcbor_state_t dec[8];
    describe_to_constraints(STOW_ID_TEST_STRUCT_ITEM, buf, sizeof(buf), dec, ARRAY_SIZE(dec));

    struct zcbor_string name;
    uint32_t type = 0;
    zassert_true(zcbor_list_start_decode(dec));
    zassert_true(zcbor_map_start_decode(dec));
    EXPECT_KEY(dec, STOW_FIELD_CONSTRAINT_NAME);
    zassert_true(zcbor_tstr_decode(dec, &name));
    EXPECT_KEY(dec, STOW_FIELD_CONSTRAINT_TYPE);
    zassert_true(zcbor_uint32_decode(dec, &type));
    EXPECT_KEY(dec, STOW_FIELD_CONSTRAINT_CONSTRAINTS);
    zassert_true(zcbor_any_skip(dec, NULL));
    zassert_true(zcbor_map_end_decode(dec));
    zassert_equal(name.len, strlen("IntField"));
    zassert_mem_equal(name.value, "IntField", name.len);
    zassert_equal(type, STOW_ITEM_TYPE_INT);
}
