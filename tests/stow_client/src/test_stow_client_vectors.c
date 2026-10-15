/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gantry/stow/stow_client_serde.h>
#include <gantry/stow/stow_wire.h>
#include <gantry/stow/types/stow_type_buffer.h>
#include <gantry/stow/types/stow_type_byte_array.h>
#include <gantry/stow/types/stow_type_enum.h>
#include <gantry/stow/types/stow_type_float.h>
#include <gantry/stow/types/stow_type_int.h>
#include <gantry/stow/types/stow_type_string.h>
#include <generated_vectors.h>
#include <string.h>
#include <zcbor_decode.h>
#include <zcbor_encode.h>

#include "test_common.h"

/**
 * @brief Codec of each item ID in the protocol suite's schema, which the vectors are recorded against
 *
 * @details The last item, a struct, has no codec in this suite; no vector gets or sets it.
 */
static const struct stow_item_interface* const vector_interfaces[] = {
    &stow_string_interface, &stow_int_interface,    &stow_string_interface,     &stow_int_interface,
    &stow_int_interface,    &stow_buffer_interface, &stow_string_interface,     &stow_string_interface,
    &stow_float_interface,  &stow_enum_interface,   &stow_byte_array_interface,
};

/**
 * @brief Vectors a correct client may send but this one never does
 *
 * @details It always names items in a Describe and only addresses items it has a codec for.
 */
static const char* const not_sent_by_this_client[] = {
    "describe_last_chunk",
    "describe_invalid_start",
    "set_unknown_item",
};

static void before_each(void* fixture)
{
    ARG_UNUSED(fixture);
    test_reset();
}

ZTEST_SUITE(stow_client_vectors, NULL, NULL, before_each, NULL, NULL);

static const struct stow_item_interface* interface_of(uint32_t id)
{
    return (id < ARRAY_SIZE(vector_interfaces)) ? vector_interfaces[id] : NULL;
}

/**
 * @brief Whether this client sends a request like the vector's
 */
static bool sent_by_this_client(const struct test_vector* vector)
{
    if (!vector->client_sends || vector->invalid_request)
    {
        return false;
    }
    for (size_t i = 0; i < ARRAY_SIZE(not_sent_by_this_client); i++)
    {
        if (strcmp(vector->name, not_sent_by_this_client[i]) == 0)
        {
            return false;
        }
    }
    return true;
}

/**
 * @brief Re-encode a vector's request with the client's encoder
 *
 * @return Encoded length, or 0 when the request does not decode as one the client encodes
 */
static size_t encode_like(const struct test_vector* vector, uint8_t* out, size_t out_size)
{
    zcbor_state_t dec[4];
    zcbor_new_decode_state(dec, ARRAY_SIZE(dec), vector->request, vector->request_len, 1, NULL, 0);
    zcbor_state_t enc[4];
    zcbor_new_encode_state(enc, ARRAY_SIZE(enc), out, out_size, 1);

    uint32_t code = 0;
    uint32_t tag = 0;
    if (!zcbor_list_start_decode(dec) || !zcbor_uint32_decode(dec, &code) || !zcbor_uint32_decode(dec, &tag))
    {
        return 0;
    }

    if (code == STOW_MSG_HELLO)
    {
        zassert_equal(stow_client_serde_encode_start(enc, STOW_MSG_HELLO, (uint8_t)tag, 0), 0);
    }
    else if (code == STOW_MSG_DESCRIBE)
    {
        static char names[4][32];
        struct stow_client_item items[4] = { 0 };
        uint32_t start_id = 0;
        size_t count = 0;
        if (!zcbor_uint32_decode(dec, &start_id) || zcbor_array_at_end(dec) || !zcbor_list_start_decode(dec))
        {
            return 0;
        }
        struct zcbor_string name;
        while (count < ARRAY_SIZE(items) && zcbor_tstr_decode(dec, &name))
        {
            memcpy(names[count], name.value, name.len);
            names[count][name.len] = '\0';
            items[count].name = names[count];
            count++;
        }
        if (!zcbor_list_end_decode(dec))
        {
            return 0;
        }
        zassert_equal(stow_client_serde_encode_describe(enc, (uint8_t)tag, start_id, items, count), 0);
        zassert_equal(stow_client_serde_describe_fit(vector->request_len, start_id, items, count), count);
        zassert_equal(stow_client_serde_describe_fit(vector->request_len - 1, start_id, items, count), count - 1);
    }
    else if (code == STOW_MSG_GET || code == STOW_MSG_SET || code == STOW_MSG_SUBSCRIBE || code == STOW_MSG_UNSUBSCRIBE)
    {
        uint32_t elements = (uint32_t)dec->elem_count;
        zassert_equal(stow_client_serde_encode_start(enc, code, (uint8_t)tag, elements), 0);
        while (!zcbor_array_at_end(dec))
        {
            uint32_t id = 0;
            zassert_true(zcbor_uint32_decode(dec, &id));
            zassert_equal(stow_client_serde_encode_id(enc, id), 0);
            if (code == STOW_MSG_SET)
            {
                const struct stow_item_interface* interface = interface_of(id);
                zassert_not_null(interface, "%s: no codec for item %u", vector->name, id);
                data_value_t value = { 0 };
                zassert_equal(interface->decode(dec, &value), 0);
                zassert_equal(interface->encode(enc, value), 0);
                interface->release(&value);
            }
        }
    }
    else
    {
        return 0;
    }

    if (!zcbor_list_end_decode(dec) || dec->payload != vector->request + vector->request_len)
    {
        return 0;
    }
    return (size_t)(enc[0].payload - out);
}

ZTEST(stow_client_vectors, test_client_encodes_every_request_it_sends_like_the_vectors)
{
    size_t checked = 0;
    for (size_t i = 0; i < ARRAY_SIZE(test_vectors); i++)
    {
        const struct test_vector* vector = &test_vectors[i];
        if (!sent_by_this_client(vector))
        {
            continue;
        }
        uint8_t encoded[64];
        size_t len = encode_like(vector, encoded, sizeof(encoded));
        zassert_equal(len, vector->request_len, "%s", vector->name);
        zassert_mem_equal(encoded, vector->request, len, "%s", vector->name);
        checked++;
    }
    zassert_true(checked > 0, "no vector is a request this client sends");
}

static void decode_response(const struct test_vector* vector)
{
    zcbor_state_t dec[4];
    zcbor_new_decode_state(dec, ARRAY_SIZE(dec), vector->response, vector->response_len, 1, NULL, 0);

    uint32_t code = 0;
    uint8_t tag = 0;
    zassert_equal(stow_wire_decode_start(dec, &code, &tag), 0, "%s", vector->name);

    switch (code)
    {
        case STOW_MSG_HELLO_RESPONSE:
        {
            struct stow_client_serde_hello hello;
            zassert_equal(stow_client_serde_decode_hello(dec, &hello), 0);
            zassert_equal(hello.roles, STOW_ROLE_GUEST | STOW_ROLE_DEV);
            zassert_true(hello.max_message > 0);
            break;
        }
        case STOW_MSG_DESCRIBE_RESPONSE:
        {
            uint32_t next_id = 0;
            struct zcbor_string chunk;
            zassert_equal(stow_client_serde_decode_describe(dec, &next_id, &chunk), 0, "%s", vector->name);
            zcbor_state_t items[4];
            zcbor_new_decode_state(items, ARRAY_SIZE(items), chunk.value, chunk.len, ZCBOR_MAX_ELEM_COUNT, NULL, 0);
            while (items[0].payload < items[0].payload_end)
            {
                struct stow_client_serde_description description;
                zassert_equal(stow_client_serde_decode_description(items, &description), 0, "%s", vector->name);
            }
            break;
        }
        case STOW_MSG_GET_RESPONSE:
            while (!zcbor_array_at_end(dec))
            {
                uint32_t id = 0;
                data_value_t value = { 0 };
                zassert_true(zcbor_uint32_decode(dec, &id));
                zassert_equal(interface_of(id)->decode(dec, &value), 0, "%s", vector->name);
                interface_of(id)->release(&value);
            }
            break;
        case STOW_MSG_ERROR:
        {
            uint32_t index = 0;
            int err = stow_client_serde_decode_error(dec, &index);
            zassert_true(err < 0 && err != -ENOMSG, "%s", vector->name);
            if (strcmp(vector->name, "get_permission_denied") == 0)
            {
                zassert_equal(err, -EACCES);
                zassert_equal(index, 0);
            }
            break;
        }
        case STOW_MSG_OK:
            zassert_true(zcbor_list_end_decode(dec), "%s", vector->name);
            break;
        default:
            zassert_unreachable("%s: unexpected response %u", vector->name, code);
    }
}

ZTEST(stow_client_vectors, test_client_decodes_every_response)
{
    for (size_t i = 0; i < ARRAY_SIZE(test_vectors); i++)
    {
        decode_response(&test_vectors[i]);
    }
}

ZTEST(stow_client_vectors, test_a_described_item_is_checked_against_its_codec)
{
    const struct test_vector* filtered = NULL;
    for (size_t i = 0; i < ARRAY_SIZE(test_vectors); i++)
    {
        filtered = (strcmp(test_vectors[i].name, "describe_filtered") == 0) ? &test_vectors[i] : filtered;
    }
    zassert_not_null(filtered);

    zcbor_state_t dec[4];
    zcbor_new_decode_state(dec, ARRAY_SIZE(dec), filtered->response, filtered->response_len, 1, NULL, 0);
    uint32_t code = 0;
    uint8_t tag = 0;
    uint32_t next_id = 0;
    struct zcbor_string chunk;
    zassert_equal(stow_wire_decode_start(dec, &code, &tag), 0);
    zassert_equal(stow_client_serde_decode_describe(dec, &next_id, &chunk), 0);
    zassert_equal(next_id, STOW_SERDE_ABSENT);

    zcbor_state_t items[4];
    zcbor_new_decode_state(items, ARRAY_SIZE(items), chunk.value, chunk.len, ZCBOR_MAX_ELEM_COUNT, NULL, 0);
    struct stow_client_serde_description description;
    zassert_equal(stow_client_serde_decode_description(items, &description), 0);
    zassert_equal(description.id, 1);
    zassert_equal(description.name.len, strlen("TestInt"));

    struct stow_client_item expected = {
        .name = "TestInt",
        .type = STOW_ITEM_TYPE_INT,
        .interface = &stow_int_interface,
        .constraints.int_constraints = { .min = -100, .max = 100 },
    };
    zassert_equal(stow_client_serde_check(&expected, &description), 0);

    expected.constraints.int_constraints.max = 99;
    zassert_equal(stow_client_serde_check(&expected, &description), -EINVAL);

    expected.type = STOW_ITEM_TYPE_FLOAT;
    expected.interface = &stow_float_interface;
    expected.constraints.float_constraints = (struct stow_float_constraints){ .min = -100.0f, .max = 100.0f };
    zassert_equal(stow_client_serde_check(&expected, &description), -EINVAL);
}
