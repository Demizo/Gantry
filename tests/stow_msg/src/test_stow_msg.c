/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gantry/stow/types/stow_types.h>
#include <string.h>

#include "test_common.h"

ZTEST_SUITE(stow_msg, NULL, NULL, NULL, NULL, NULL);

static const struct stow_item_const_metadata* item(enum stow_item_id id) { return &g_stow_const_metadata[id]; }

static struct stow_msg_entry int_entry(uint32_t id, int value)
{
    return (struct stow_msg_entry){
        .id = id,
        .codec = item(STOW_ID_TEST_INT)->interface,
        .value = { .type = STOW_ITEM_TYPE_INT, .data.int_value = value },
    };
}

static size_t finish(struct stow_msg_writer* writer)
{
    size_t len = 0;
    zassert_ok(stow_msg_finish(writer, &len));
    return len;
}

/**
 * @brief Find the smallest budget a Describe Response holding only @p described fits
 */
static size_t smallest_budget(const struct stow_item_const_metadata* described, uint8_t* out)
{
    for (size_t budget = 0;; budget++)
    {
        struct stow_msg_writer writer;
        stow_msg_writer_init(&writer, out, budget);
        (void)stow_msg_write(&writer, &(struct stow_msg){ .code = STOW_MSG_DESCRIBE_RESPONSE, .tag = 1 });
        if (stow_msg_put_description(&writer, described) == 0)
        {
            return budget;
        }
    }
}

ZTEST(stow_msg, test_an_error_replaces_a_partly_written_get_response)
{
    uint8_t out[64];
    struct stow_msg_writer writer;
    stow_msg_writer_init(&writer, out, sizeof(out));
    zassert_ok(stow_msg_write(&writer, &(struct stow_msg){ .code = STOW_MSG_GET_RESPONSE, .tag = 5, .count = 2 }));
    struct stow_msg_entry first = int_entry(STOW_ID_TEST_INT, 7);
    zassert_ok(stow_msg_put_entry(&writer, &first));

    struct stow_msg error = { .code = STOW_MSG_ERROR, .tag = 5, .error = { -EACCES, STOW_ID_TEST_STRING, 1 } };
    zassert_ok(stow_msg_write(&writer, &error));

    const uint8_t expected[] = { 0x85, STOW_MSG_ERROR, 5, STOW_ERR_PERMISSION_DENIED, STOW_ID_TEST_STRING, 1 };
    zassert_equal(finish(&writer), sizeof(expected));
    zassert_mem_equal(out, expected, sizeof(expected));
}

ZTEST(stow_msg, test_the_first_error_sticks_until_the_next_message)
{
    uint8_t out[8];
    struct stow_msg_writer writer;
    stow_msg_writer_init(&writer, out, 6); /* one entry fits, a second does not */
    zassert_ok(stow_msg_write(&writer, &(struct stow_msg){ .code = STOW_MSG_GET_RESPONSE, .tag = 1, .count = 3 }));
    struct stow_msg_entry entry = int_entry(STOW_ID_TEST_INT, 7);
    zassert_ok(stow_msg_put_entry(&writer, &entry));
    zassert_equal(stow_msg_put_entry(&writer, &entry), -E2BIG);
    zassert_equal(stow_msg_put_entry(&writer, &entry), -E2BIG);

    size_t len = 0;
    zassert_equal(stow_msg_finish(&writer, &len), -E2BIG);
    zassert_ok(stow_msg_write(&writer, &(struct stow_msg){ .code = STOW_MSG_OK, .tag = 1 }));
    zassert_equal(finish(&writer), 3);
}

ZTEST(stow_msg, test_an_entry_that_does_not_fit_is_too_large)
{
    uint8_t out[16];
    struct stow_msg_writer writer;
    stow_msg_writer_init(&writer, out, sizeof(out));
    zassert_ok(stow_msg_write(&writer, &(struct stow_msg){ .code = STOW_MSG_GET_RESPONSE, .tag = 1, .count = 1 }));
    struct stow_msg_entry entry = {
        .id = STOW_ID_TEST_STRING,
        .codec = item(STOW_ID_TEST_STRING)->interface,
        .value = { .type = STOW_ITEM_TYPE_STRING, .data.string_value = "longer than sixteen bytes" },
    };
    zassert_equal(stow_msg_put_entry(&writer, &entry), -E2BIG);
}

ZTEST(stow_msg, test_a_description_that_does_not_fit_is_rolled_back_and_becomes_next)
{
    uint8_t out[512];
    size_t budget = smallest_budget(item(STOW_ID_TEST_INT), out);

    struct stow_msg_writer writer;
    stow_msg_writer_init(&writer, out, budget);
    zassert_ok(stow_msg_write(&writer, &(struct stow_msg){ .code = STOW_MSG_DESCRIBE_RESPONSE, .tag = 1 }));
    zassert_ok(stow_msg_put_description(&writer, item(STOW_ID_TEST_INT)));
    zassert_equal(stow_msg_put_description(&writer, item(STOW_ID_TEST_STRING)), -E2BIG);
    zassert_equal(
        stow_msg_put_description(&writer, item(STOW_ID_TEST_ENUM)), -E2BIG, "a later put fit after a rejection");
    size_t len = finish(&writer);

    struct stow_msg_reader reader;
    struct stow_msg msg;
    zassert_ok(stow_msg_read(&reader, out, len, &test_items, &msg));
    zassert_equal(msg.count, 1);
    zassert_equal(msg.describe_response.next, STOW_ID_TEST_STRING);
    struct stow_msg_description description;
    zassert_ok(stow_msg_next_description(&reader, &description));
    zassert_equal(description.id, STOW_ID_TEST_INT);
}

ZTEST(stow_msg, test_next_is_null_when_no_description_is_rejected)
{
    uint8_t out[512];
    struct stow_msg_writer writer;
    stow_msg_writer_init(&writer, out, sizeof(out));
    zassert_ok(stow_msg_write(&writer, &(struct stow_msg){ .code = STOW_MSG_DESCRIBE_RESPONSE, .tag = 1 }));
    zassert_ok(stow_msg_put_description(&writer, item(STOW_ID_TEST_INT)));
    zassert_ok(stow_msg_put_description(&writer, item(STOW_ID_TEST_STRING)));
    size_t len = finish(&writer);

    struct stow_msg_reader reader;
    struct stow_msg msg;
    zassert_ok(stow_msg_read(&reader, out, len, &test_items, &msg));
    zassert_equal(msg.count, 2);
    zassert_equal(msg.describe_response.next, STOW_MSG_ABSENT);
}

ZTEST(stow_msg, test_an_empty_describe_response_is_valid)
{
    uint8_t out[16];
    struct stow_msg_writer writer;
    stow_msg_writer_init(&writer, out, sizeof(out));
    zassert_ok(stow_msg_write(&writer, &(struct stow_msg){ .code = STOW_MSG_DESCRIBE_RESPONSE, .tag = 4 }));

    const uint8_t expected[] = { 0x84, STOW_MSG_DESCRIBE_RESPONSE, 4, 0xF6, 0x40 }; /* [3, 4, null, h''] */
    zassert_equal(finish(&writer), sizeof(expected));
    zassert_mem_equal(out, expected, sizeof(expected));
}

ZTEST(stow_msg, test_a_list_whose_first_element_is_rejected_is_too_large)
{
    uint8_t out[512];
    size_t budget = smallest_budget(item(STOW_ID_TEST_INT), out) - 1;

    struct stow_msg_writer writer;
    stow_msg_writer_init(&writer, out, budget);
    zassert_ok(stow_msg_write(&writer, &(struct stow_msg){ .code = STOW_MSG_DESCRIBE_RESPONSE, .tag = 1 }));
    zassert_equal(stow_msg_put_description(&writer, item(STOW_ID_TEST_INT)), -E2BIG);
    size_t len = 0;
    zassert_equal(stow_msg_finish(&writer, &len), -E2BIG);
}

ZTEST(stow_msg, test_a_group_of_names_that_fits_fits_from_any_start)
{
    static const char* const names[] = { "TestInt", "TestString", "TestSessionInt", "TestDevInt", "TestBuffer" };
    uint8_t out[40];
    struct stow_msg_writer writer;
    stow_msg_writer_init(&writer, out, sizeof(out));
    zassert_ok(stow_msg_write(
        &writer, &(struct stow_msg){ .code = STOW_MSG_DESCRIBE, .tag = 1, .describe = { .start = 0, .named = true } }));
    size_t fit = 0;
    while (fit < ARRAY_SIZE(names) && stow_msg_put_name(&writer, names[fit]) == 0)
    {
        fit++;
    }
    zassert_true(fit > 0 && fit < ARRAY_SIZE(names), "the budget should split the names");

    zassert_ok(stow_msg_write(
        &writer, &(struct stow_msg){ .code = STOW_MSG_DESCRIBE,
                                     .tag = STOW_TAG_MAX,
                                     .describe = { .start = UINT32_MAX - 1U, .named = true } }));
    for (size_t i = 0; i < fit; i++)
    {
        zassert_ok(stow_msg_put_name(&writer, names[i]));
    }
    zassert_true(finish(&writer) <= sizeof(out));
}

ZTEST(stow_msg, test_a_hello_response_reports_the_writers_budget)
{
    uint8_t out[128];
    struct stow_msg_writer writer;
    stow_msg_writer_init(&writer, out, 100);
    struct stow_msg hello = {
        .code = STOW_MSG_HELLO_RESPONSE,
        .tag = 2,
        .hello = {
            .max_message = 9999,
            .stow_hash = { .value = (const uint8_t*)"hash", .len = 4 },
            .roles = 1,
            .role_names = g_stow_role_names,
            .role_count = STOW_ROLE_COUNT,
        },
    };
    zassert_ok(stow_msg_write(&writer, &hello));
    size_t len = finish(&writer);

    struct stow_msg_reader reader;
    struct stow_msg msg;
    zassert_ok(stow_msg_read(&reader, out, len, &test_items, &msg));
    zassert_equal(msg.hello.max_message, 100);
    zassert_equal(msg.hello.roles, 1);
    zassert_equal(msg.hello.stow_hash.len, 4);
}

ZTEST(stow_msg, test_unknown_items_pass_the_read_and_are_reported_by_the_walk)
{
    const uint8_t set[] = { 0x84, STOW_MSG_SET, 1, 0x18, 99, 5 };                /* [6, 1, 99, 5] */
    const uint8_t get[] = { 0x84, STOW_MSG_GET, 1, STOW_ID_TEST_INT, 0x18, 99 }; /* [4, 1, 1, 99] */

    struct stow_msg_reader reader;
    struct stow_msg msg;
    struct stow_msg_entry entry;
    zassert_ok(stow_msg_read(&reader, set, sizeof(set), &test_items, &msg));
    zassert_equal(msg.count, 1);
    zassert_equal(stow_msg_next_entry(&reader, &entry), -ENOENT);
    zassert_equal(entry.id, 99);

    zassert_ok(stow_msg_read(&reader, get, sizeof(get), &test_items, &msg));
    zassert_equal(msg.count, 2);
    zassert_ok(stow_msg_next_entry(&reader, &entry));
    zassert_equal(entry.local, STOW_ID_TEST_INT);
    stow_msg_entry_release(&entry);
    zassert_equal(stow_msg_next_entry(&reader, &entry), -ENOENT);
}

ZTEST(stow_msg, test_unknown_hello_keys_are_skipped)
{
    /* [1, 0, 1, {0: 200, 1: "h", 2: 1, 3: ["Guest"], 9: "later"}] */
    const uint8_t hello[] = { 0x84, STOW_MSG_HELLO_RESPONSE,
                              0,    STOW_PROTOCOL_VERSION,
                              0xA5, 0x00,
                              0x18, 200,
                              0x01, 0x61,
                              'h',  0x02,
                              0x01, 0x03,
                              0x81, 0x65,
                              'G',  'u',
                              'e',  's',
                              't',  0x09,
                              0x65, 'l',
                              'a',  't',
                              'e',  'r' };
    struct stow_msg_reader reader;
    struct stow_msg msg;
    zassert_ok(stow_msg_read(&reader, hello, sizeof(hello), &test_items, &msg));
    zassert_equal(msg.hello.max_message, 200);
    zassert_equal(msg.hello.roles, 1);
}

ZTEST(stow_msg, test_an_update_with_a_tag_is_malformed_without_a_tag)
{
    const uint8_t update[] = { 0x84, STOW_MSG_UPDATE, 5, STOW_ID_TEST_INT, 42 }; /* [9, 5, 1, 42] */
    struct stow_msg_reader reader;
    struct stow_msg msg;
    zassert_equal(stow_msg_read(&reader, update, sizeof(update), &test_items, &msg), -EBADMSG);
    zassert_equal(msg.tag, STOW_TAG_NONE);
}

ZTEST(stow_msg, test_each_errno_travels_as_its_error_code)
{
    static const struct
    {
        int err;
        uint8_t code;
    } errnos[] = {
        { -EBADMSG, STOW_ERR_MALFORMED_MSG },    { -EPROTO, STOW_ERR_UNKNOWN_MSG },
        { -ENOENT, STOW_ERR_INVALID_ITEM },      { -ENOMEM, STOW_ERR_OUT_OF_MEMORY },
        { -EACCES, STOW_ERR_PERMISSION_DENIED }, { -EINVAL, STOW_ERR_INVALID_VALUE },
        { -EALREADY, STOW_ERR_ALREADY_SET },     { -E2BIG, STOW_ERR_TOO_LARGE },
    };
    for (size_t i = 0; i < ARRAY_SIZE(errnos); i++)
    {
        uint8_t out[8];
        struct stow_msg_writer writer;
        stow_msg_writer_init(&writer, out, sizeof(out));
        struct stow_msg error = { .code = STOW_MSG_ERROR,
                                  .tag = 1,
                                  .error = { errnos[i].err, STOW_MSG_ABSENT, STOW_MSG_ABSENT } };
        zassert_ok(stow_msg_write(&writer, &error));
        size_t len = finish(&writer);
        zassert_equal(out[3], errnos[i].code);

        struct stow_msg_reader reader;
        struct stow_msg msg;
        zassert_ok(stow_msg_read(&reader, out, len, &test_items, &msg));
        zassert_equal(msg.error.err, errnos[i].err);
    }
}

ZTEST(stow_msg, test_other_errnos_and_codes_are_unknown)
{
    uint8_t out[8];
    struct stow_msg_writer writer;
    stow_msg_writer_init(&writer, out, sizeof(out));
    zassert_ok(stow_msg_write(
        &writer, &(struct stow_msg){
                     .code = STOW_MSG_ERROR, .tag = 1, .error = { -ETIMEDOUT, STOW_MSG_ABSENT, STOW_MSG_ABSENT } }));
    size_t len = finish(&writer);
    zassert_equal(out[3], STOW_ERR_UNKNOWN);

    struct stow_msg_reader reader;
    struct stow_msg msg;
    zassert_ok(stow_msg_read(&reader, out, len, &test_items, &msg));
    zassert_equal(msg.error.err, -EIO);

    const uint8_t later[] = { 0x83, STOW_MSG_ERROR, 1, 0x18, 42 }; /* [11, 1, 42] */
    zassert_ok(stow_msg_read(&reader, later, sizeof(later), &test_items, &msg));
    zassert_equal(msg.error.err, -EIO);
}

ZTEST(stow_msg, test_constraints_nested_deeper_than_this_side_decodes_are_read)
{
    /* [3, 1, null, <<{0: 1, 1: "X", 6: 1, 8: [[...0...]]}>>], the constraints STOW_DEPTH_MAX + 3 lists deep */
    const size_t depth = STOW_DEPTH_MAX + 3;
    const size_t description_len = 10 + depth;
    uint8_t response[64] = { 0x84,
                             STOW_MSG_DESCRIBE_RESPONSE,
                             1,
                             0xF6,
                             (uint8_t)(0x40 | description_len),
                             0xA4,
                             0x00,
                             0x01,
                             0x01,
                             0x61,
                             'X',
                             0x06,
                             0x01,
                             0x08 };
    zassert_true(description_len < 24 && 14 + depth + 1 <= sizeof(response));
    memset(&response[14], 0x81, depth);
    response[14 + depth] = 0x00;

    struct stow_msg_reader reader;
    struct stow_msg msg;
    zassert_ok(stow_msg_read(&reader, response, 15 + depth, &test_items, &msg));
    zassert_equal(msg.count, 1);
    struct stow_msg_description description;
    zassert_ok(stow_msg_next_description(&reader, &description));
    zassert_equal(description.constraints.len, depth + 1);
}
