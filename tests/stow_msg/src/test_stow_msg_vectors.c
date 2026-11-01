/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

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
 * @return The written length, or 0 when an entry names an item the fixture lacks, whose value cannot be written
 */
static size_t write_back(struct stow_msg_reader* reader, const struct stow_msg* msg, uint8_t* out)
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
                return 0;
            }
            zassert_ok(ret);
            zassert_ok(stow_msg_put_entry(&writer, &entry));
            stow_msg_entry_release(&entry);
        }
    }

    size_t len = 0;
    zassert_ok(stow_msg_finish(&writer, &len));
    return len;
}

ZTEST(stow_msg_vectors, test_every_exchange_step_reads_and_writes_back)
{
    for (size_t vector_index = 0; vector_index < stow_exchanges_count; vector_index++)
    {
        const struct protocol_vector* vector = stow_exchanges[vector_index];
        for (size_t step_index = 0; step_index < vector->step_count; step_index++)
        {
            const struct protocol_step* step = &vector->steps[step_index];
            struct stow_msg_reader reader;
            struct stow_msg msg;
            int ret = stow_msg_read(&reader, step->data, step->len, &test_items, &msg);
            if ((ret == -EINVAL || ret == -EBADMSG) && step->side == CLIENT_SIDE && step_index + 1 < vector->step_count)
            {
                /* A request breaking a constraint or a limit of this side fails the read with the server's Error */
                const struct protocol_step* answer = &vector->steps[step_index + 1];
                struct stow_msg error;
                zassert_ok(stow_msg_read(&reader, answer->data, answer->len, &test_items, &error), "%s", vector->name);
                zassert_equal(error.error.err, ret, "%s: %s", vector->name, answer->edn);
                zassert_equal(error.error.item, msg.error.item, "%s: %s", vector->name, answer->edn);
                continue;
            }
            zassert_ok(ret, "%s step %zu: %s", vector->name, step_index, step->edn);

            uint8_t out[OUT_SIZE];
            size_t len = write_back(&reader, &msg, out);
            if (len == 0)
            {
                continue;
            }
            zassert_equal(len, step->len, "%s step %zu: %s", vector->name, step_index, step->edn);
            zassert_mem_equal(out, step->data, len, "%s step %zu: %s", vector->name, step_index, step->edn);
        }
    }
}

ZTEST(stow_msg_vectors, test_every_malformed_request_fails_with_the_error_its_answer_names)
{
    for (size_t vector_index = 0; vector_index < stow_malformed_count; vector_index++)
    {
        const struct protocol_vector* vector = stow_malformed[vector_index];
        const struct protocol_step* request = &vector->steps[0];
        const struct protocol_step* answer = &vector->steps[1];

        uint32_t code = STOW_MSG_ABSENT;
        uint8_t tag = STOW_TAG_NONE;
        (void)stow_msg_peek(request->data, request->len, &code, &tag);
        if (code < STOW_MSG_COUNT && !stow_msg_is_request(code))
        {
            /* The server answers a response or Update from its code and tag alone */
            continue;
        }

        struct stow_msg_reader reader;
        struct stow_msg failed;
        int ret = stow_msg_read(&reader, request->data, request->len, &test_items, &failed);
        zassert_not_equal(ret, 0, "%s read", vector->name);
        zassert_equal(ret, failed.error.err, "%s", vector->name);

        struct stow_msg error;
        zassert_ok(
            stow_msg_read(&reader, answer->data, answer->len, &test_items, &error), "%s: %s", vector->name,
            answer->edn);
        zassert_equal(error.code, STOW_MSG_ERROR, "%s", vector->name);
        zassert_equal(error.error.err, ret, "%s: %s", vector->name, answer->edn);
        zassert_equal(error.tag, failed.tag, "%s: %s", vector->name, answer->edn);

        uint8_t out[OUT_SIZE];
        struct stow_msg_writer writer;
        stow_msg_writer_init(&writer, out, budget);
        zassert_ok(stow_msg_write(
            &writer, &(struct stow_msg){ .code = STOW_MSG_ERROR, .tag = failed.tag, .error = failed.error }));
        size_t len = 0;
        zassert_ok(stow_msg_finish(&writer, &len));
        zassert_equal(len, answer->len, "%s: %s", vector->name, answer->edn);
        zassert_mem_equal(out, answer->data, len, "%s: %s", vector->name, answer->edn);
    }
}
