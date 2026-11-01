/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gantry/error.h>
#include <stow_vectors.h>
#include <string.h>

#include "test_common.h"

/**
 * @brief Largest message written back; a vector's message is never longer than its Hello's max_message
 */
#define OUT_SIZE 512

/**
 * @brief Index of the client in stow_sides
 */
#define CLIENT_SIDE 0

/**
 * @brief The budget the vectors were recorded with, from their first Hello Response
 */
static size_t budget;

static void* setup(void)
{
    for (size_t vector_index = 0; budget == 0 && vector_index < stow_exchanges_count; vector_index++)
    {
        for (size_t step_index = 0; step_index < stow_exchanges[vector_index]->step_count; step_index++)
        {
            const struct protocol_step* step = &stow_exchanges[vector_index]->steps[step_index];
            struct stow_msg_reader reader;
            struct stow_msg msg;
            if (stow_msg_read(&reader, step->data, step->len, &test_items, &msg) == 0 &&
                msg.code == STOW_MSG_HELLO_RESPONSE)
            {
                budget = msg.hello.max_message;
                break;
            }
        }
    }
    zassert_true(budget > 0 && budget <= OUT_SIZE, "No Hello Response gives the vectors' budget");
    return NULL;
}

ZTEST_SUITE(stow_msg_vectors, NULL, setup, NULL, NULL, NULL);

/**
 * @brief Write a message back from what was read, walking its list
 *
 * @return SUCCESS, or -ENOENT when an entry names an item the fixture lacks, whose value cannot be written
 */
static int write_back(struct stow_msg_reader* reader, const struct stow_msg* msg, uint8_t* out, size_t* len)
{
    struct stow_msg written = *msg;
    if (msg->code == STOW_MSG_HELLO_RESPONSE)
    {
        written.hello.role_names = g_stow_role_names;
        written.hello.role_count = STOW_ROLE_COUNT;
    }

    struct stow_msg_writer writer;
    stow_msg_writer_init(&writer, out, budget);
    zassert_ok(stow_msg_write(&writer, &written));

    if (msg->code == STOW_MSG_DESCRIBE && msg->describe.named)
    {
        for (uint16_t i = 0; i < msg->count; i++)
        {
            struct zcbor_string name;
            char text[64] = { 0 };
            zassert_ok(stow_msg_next_name(reader, &name));
            zassert_true(name.len < sizeof(text));
            memcpy(text, name.value, name.len);
            zassert_ok(stow_msg_put_name(&writer, text));
        }
    }
    else if (msg->code == STOW_MSG_DESCRIBE_RESPONSE)
    {
        for (uint16_t i = 0; i < msg->count; i++)
        {
            struct stow_msg_description description;
            zassert_ok(stow_msg_next_description(reader, &description));
            zassert_ok(stow_msg_put_description(&writer, &g_stow_const_metadata[description.id]));
        }
        if (msg->describe_response.next != STOW_MSG_ABSENT)
        {
            zassert_equal(
                stow_msg_put_description(&writer, &g_stow_const_metadata[msg->describe_response.next]), -E2BIG);
        }
    }
    else
    {
        for (uint16_t i = 0; i < msg->count; i++)
        {
            struct stow_msg_entry entry;
            int ret = stow_msg_next_entry(reader, &entry);
            if (ret == -ENOENT)
            {
                return ret;
            }
            zassert_ok(ret);
            zassert_ok(stow_msg_put_entry(&writer, &entry));
            stow_msg_entry_release(&entry);
        }
    }

    zassert_ok(stow_msg_finish(&writer, len));
    return SUCCESS;
}

/**
 * @brief Check that written bytes are a step's, printing them in hex for `protocol-spec diag` when they aren't
 */
static void expect_step(const struct protocol_vector* vector, size_t index, const uint8_t* out, size_t len)
{
    const struct protocol_step* step = &vector->steps[index];
    bool same = len == step->len && memcmp(out, step->data, len) == 0;
    if (!same)
    {
        protocol_vector_print_hex("wrote", out, len);
    }
    zassert_true(same, "%s.steps[%zu]: expected %s", vector->name, index, step->edn);
}

/**
 * @brief Check that the step after a request this side failed to read is the Error written from the failure
 */
static void check_failure_answered(
    const struct protocol_vector* vector, size_t index, int ret, const struct stow_msg* failed)
{
    zassert_true(index + 1 < vector->step_count, "%s.steps[%zu]: failed with %d, unanswered", vector->name, index, ret);
    const struct protocol_step* answer = &vector->steps[index + 1];

    struct stow_msg_reader reader;
    struct stow_msg error;
    zassert_ok(
        stow_msg_read(&reader, answer->data, answer->len, &test_items, &error), "%s: %s", vector->name, answer->edn);
    zassert_equal(error.code, STOW_MSG_ERROR, "%s: %s", vector->name, answer->edn);
    zassert_equal(error.error.err, ret, "%s: %s", vector->name, answer->edn);

    uint8_t out[OUT_SIZE];
    struct stow_msg_writer writer;
    stow_msg_writer_init(&writer, out, budget);
    zassert_ok(stow_msg_write(
        &writer, &(struct stow_msg){ .code = STOW_MSG_ERROR, .tag = failed->tag, .error = failed->error }));
    size_t len = 0;
    zassert_ok(stow_msg_finish(&writer, &len));
    expect_step(vector, index + 1, out, len);
}

/**
 * @brief Check that a step reads and writes back to the same bytes, or fails to read with the Error that answers it
 *
 * @return NULL, or why the step does not apply to the codec
 */
static const char* check_step(const struct protocol_vector* vector, size_t index)
{
    const struct protocol_step* step = &vector->steps[index];
    struct stow_msg_reader reader;
    struct stow_msg msg;
    int ret = stow_msg_read(&reader, step->data, step->len, &test_items, &msg);
    if (ret != 0 && step->side == CLIENT_SIDE)
    {
        check_failure_answered(vector, index, ret, &msg);
        return NULL;
    }
    zassert_ok(ret, "%s.steps[%zu]: %s", vector->name, index, step->edn);

    uint8_t out[OUT_SIZE];
    size_t len = 0;
    if (write_back(&reader, &msg, out, &len) == -ENOENT)
    {
        return "an entry names an item the fixture lacks, whose value can't be written";
    }
    expect_step(vector, index, out, len);
    return NULL;
}

/**
 * @brief Check that a malformed vector's request fails to read with the Error that answers it
 *
 * @return NULL, or why the request does not apply to the codec
 */
static const char* check_malformed_request(const struct protocol_vector* vector)
{
    const struct protocol_step* request = &vector->steps[0];
    uint32_t code = STOW_MSG_ABSENT;
    uint8_t tag = STOW_TAG_NONE;
    (void)stow_msg_peek(request->data, request->len, &code, &tag);
    if (code < STOW_MSG_COUNT && !stow_msg_is_request(code))
    {
        return "the server answers a message that isn't a request from its code and tag alone";
    }

    struct stow_msg_reader reader;
    struct stow_msg failed;
    int ret = stow_msg_read(&reader, request->data, request->len, &test_items, &failed);
    zassert_not_equal(ret, 0, "%s: the request was read", vector->name);
    check_failure_answered(vector, 0, ret, &failed);
    return NULL;
}

/**
 * @brief Log why a step does not apply to the codec
 *
 * @return Whether the step was skipped
 */
static bool skipped(const struct protocol_vector* vector, size_t index, const char* reason)
{
    if (reason != NULL)
    {
        TC_PRINT("%s.steps[%zu] skipped: %s\n", vector->name, index, reason);
    }
    return reason != NULL;
}

/**
 * @brief Check every step from @p first on, then skip the test if any step did not apply, so a skip never hides a
 * failure
 *
 * @param earlier_skipped Whether a step before @p first did not apply
 */
static void check_steps(const struct protocol_vector* vector, size_t first, bool earlier_skipped)
{
    bool any_skipped = earlier_skipped;
    for (size_t i = first; i < vector->step_count; i++)
    {
        any_skipped |= skipped(vector, i, check_step(vector, i));
    }
    if (any_skipped)
    {
        ztest_test_skip();
    }
}

/**
 * @brief Check an exchange
 */
static void check(const struct protocol_vector* vector) { check_steps(vector, 0, false); }

/**
 * @brief Check a malformed vector, whose request fails to read with the Error that answers it
 */
static void check_malformed(const struct protocol_vector* vector)
{
    check_steps(vector, 1, skipped(vector, 0, check_malformed_request(vector)));
}

#define CHECK(name) \
    ZTEST(stow_msg_vectors, test_##name) { check(&stow_vector_##name); }

#define CHECK_MALFORMED(name) \
    ZTEST(stow_msg_vectors, test_##name) { check_malformed(&stow_vector_##name); }

STOW_EXCHANGES(CHECK)
STOW_MALFORMED(CHECK_MALFORMED)
