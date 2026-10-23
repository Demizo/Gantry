/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gantry/stow/stow_client_serde.h>
#include <gantry/stow/stow_wire.h>
#include <gantry/stow/types/stow_type_float.h>
#include <generated_vec_stow_client.h>
#include <protocol_vectors.h>
#include <string.h>
#include <zcbor_decode.h>
#include <zcbor_encode.h>

#include "test_common.h"

#define CLIENT_SIDE 0
#define MAX_WIRE_ID 32

PROTOCOL_VECTORS_DECLARE(stow);

/* The binding of each wire ID, matched by name through the vectors' Describe Responses */
static const struct stow_client_item* bound[MAX_WIRE_ID];

static void before_each(void* fixture)
{
    ARG_UNUSED(fixture);
    test_reset();
}

ZTEST_SUITE(stow_client_vectors, NULL, NULL, before_each, NULL, NULL);

static const struct stow_client_item* binding_named(struct zcbor_string name)
{
    for (size_t i = 0; i < vec_stow_client_schema.item_count; i++)
    {
        const struct stow_client_item* item = &vec_stow_client_schema.items[i];
        if (strlen(item->name) == name.len && memcmp(item->name, name.value, name.len) == 0)
        {
            return item;
        }
    }
    return NULL;
}

/**
 * @brief Call @p visit for every item description in the exchanges' Describe Responses
 */
static void for_each_description(void (*visit)(const struct stow_client_serde_description* description))
{
    for (size_t v = 0; v < stow_exchanges_count; v++)
    {
        const struct protocol_vector* vector = &stow_exchanges[v];
        for (size_t s = 0; s < vector->step_count; s++)
        {
            const struct protocol_step* step = &vector->steps[s];
            zcbor_state_t dec[4];
            zcbor_new_decode_state(dec, ARRAY_SIZE(dec), step->data, step->len, 1, NULL, 0);
            uint32_t code = 0;
            uint8_t tag = 0;
            if (step->side == CLIENT_SIDE || stow_wire_decode_start(dec, &code, &tag) != 0 ||
                code != STOW_MSG_DESCRIBE_RESPONSE)
            {
                continue;
            }
            uint32_t next_id = 0;
            struct zcbor_string chunk;
            zassert_equal(stow_client_serde_decode_describe(dec, &next_id, &chunk), 0, "%s", vector->name);
            zcbor_state_t items[4];
            zcbor_new_decode_state(items, ARRAY_SIZE(items), chunk.value, chunk.len, ZCBOR_MAX_ELEM_COUNT, NULL, 0);
            while (items[0].payload < items[0].payload_end)
            {
                struct stow_client_serde_description description;
                zassert_equal(stow_client_serde_decode_description(items, &description), 0, "%s", vector->name);
                visit(&description);
            }
        }
    }
}

static void bind(const struct stow_client_serde_description* description)
{
    zassert_true(description->id < MAX_WIRE_ID);
    bound[description->id] = binding_named(description->name);
}

static const struct stow_item_interface* interface_of(uint32_t id)
{
    return (id < MAX_WIRE_ID && bound[id] != NULL) ? bound[id]->interface : NULL;
}

/**
 * @brief Whether the client could name a request's items as the step does
 */
static bool sendable_items(const struct protocol_step* step, uint32_t code)
{
    zcbor_state_t dec[4];
    zcbor_new_decode_state(dec, ARRAY_SIZE(dec), step->data, step->len, 1, NULL, 0);
    uint32_t header = 0;
    zassert_true(
        zcbor_list_start_decode(dec) && zcbor_uint32_decode(dec, &header) && zcbor_uint32_decode(dec, &header));
    uint32_t set_ids[8];
    size_t set_count = 0;
    while (!zcbor_array_at_end(dec))
    {
        uint32_t id = 0;
        zassert_true(zcbor_uint32_decode(dec, &id));
        if (interface_of(id) == NULL)
        {
            return false;
        }
        if (code != STOW_MSG_SET)
        {
            continue;
        }
        for (size_t i = 0; i < set_count; i++)
        {
            if (set_ids[i] == id)
            {
                return false;
            }
        }
        zassert_true(set_count < ARRAY_SIZE(set_ids));
        set_ids[set_count++] = id;
        zassert_true(zcbor_any_skip(dec, NULL));
    }
    return true;
}

/**
 * @brief Re-encode a client step with the client's encoder
 *
 * @return Encoded length, or 0 for a request this client never sends: a Describe that names no items, an item it
 * has no binding for, or a Set that names an item twice
 */
static size_t encode_like(
    const struct protocol_vector* vector, const struct protocol_step* step, uint8_t* out, size_t out_size)
{
    zcbor_state_t dec[4];
    zcbor_new_decode_state(dec, ARRAY_SIZE(dec), step->data, step->len, 1, NULL, 0);
    zcbor_state_t enc[4];
    zcbor_new_encode_state(enc, ARRAY_SIZE(enc), out, out_size, 1);

    uint32_t code = 0;
    uint32_t tag = 0;
    zassert_true(zcbor_list_start_decode(dec) && zcbor_uint32_decode(dec, &code) && zcbor_uint32_decode(dec, &tag));

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
        zassert_true(zcbor_uint32_decode(dec, &start_id));
        if (zcbor_array_at_end(dec))
        {
            return 0;
        }
        zassert_true(zcbor_list_start_decode(dec));
        struct zcbor_string name;
        while (count < ARRAY_SIZE(items) && zcbor_tstr_decode(dec, &name))
        {
            memcpy(names[count], name.value, name.len);
            names[count][name.len] = '\0';
            items[count].name = names[count];
            count++;
        }
        zassert_true(zcbor_list_end_decode(dec));
        zassert_equal(stow_client_serde_encode_describe(enc, (uint8_t)tag, start_id, items, count), 0);
        zassert_equal(stow_client_serde_describe_fit(step->len, start_id, items, count), count);
        zassert_equal(stow_client_serde_describe_fit(step->len - 1, start_id, items, count), count - 1);
    }
    else
    {
        if (!sendable_items(step, code))
        {
            return 0;
        }
        zassert_equal(stow_client_serde_encode_start(enc, code, (uint8_t)tag, (uint32_t)dec->elem_count), 0);
        while (!zcbor_array_at_end(dec))
        {
            uint32_t id = 0;
            zassert_true(zcbor_uint32_decode(dec, &id));
            zassert_equal(stow_client_serde_encode_id(enc, id), 0);
            if (code == STOW_MSG_SET)
            {
                data_value_t value = { 0 };
                zassert_equal(stow_item_decode(interface_of(id), dec, &value), 0, "%s", vector->name);
                int ret = interface_of(id)->encode(enc, value);
                stow_item_release(interface_of(id), &value);
                zassert_equal(ret, 0, "%s", vector->name);
            }
        }
    }

    zassert_true(zcbor_list_end_decode(dec), "%s", vector->name);
    return (size_t)(enc[0].payload - out);
}

ZTEST(stow_client_vectors, test_client_encodes_every_request_it_sends_like_the_vectors)
{
    for_each_description(bind);
    size_t checked = 0;
    for (size_t v = 0; v < stow_exchanges_count; v++)
    {
        const struct protocol_vector* vector = &stow_exchanges[v];
        for (size_t s = 0; s < vector->step_count; s++)
        {
            const struct protocol_step* step = &vector->steps[s];
            uint8_t encoded[64];
            size_t len = (step->side == CLIENT_SIDE) ? encode_like(vector, step, encoded, sizeof(encoded)) : 0;
            if (len == 0)
            {
                continue;
            }
            zassert_equal(len, step->len, "%s.steps[%zu]: expected %s", vector->name, s, step->edn);
            zassert_mem_equal(encoded, step->data, len, "%s.steps[%zu]: expected %s", vector->name, s, step->edn);
            checked++;
        }
    }
    zassert_true(checked > 0, "no step is a request this client sends");
}

static void decode_response(const struct protocol_vector* vector, size_t index)
{
    const struct protocol_step* step = &vector->steps[index];
    zcbor_state_t dec[4];
    zcbor_new_decode_state(dec, ARRAY_SIZE(dec), step->data, step->len, 1, NULL, 0);

    uint32_t code = 0;
    uint8_t tag = 0;
    zassert_equal(stow_wire_decode_start(dec, &code, &tag), 0, "%s.steps[%zu]", vector->name, index);

    switch (code)
    {
        case STOW_MSG_HELLO_RESPONSE:
        {
            struct stow_client_serde_hello hello;
            zassert_equal(stow_client_serde_decode_hello(dec, &hello), 0);
            zassert_equal(hello.roles == 0, vector->roles[0] == NULL, "%s", vector->name);
            zassert_true(hello.max_message > 0);
            break;
        }
        case STOW_MSG_DESCRIBE_RESPONSE:
        {
            uint32_t next_id = 0;
            struct zcbor_string chunk;
            zassert_equal(stow_client_serde_decode_describe(dec, &next_id, &chunk), 0, "%s", vector->name);
            break;
        }
        case STOW_MSG_GET_RESPONSE:
        case STOW_MSG_UPDATE:
            while (!zcbor_array_at_end(dec))
            {
                uint32_t id = 0;
                data_value_t value = { 0 };
                zassert_true(zcbor_uint32_decode(dec, &id));
                zassert_not_null(interface_of(id), "%s: no binding for item %u", vector->name, id);
                zassert_equal(stow_item_decode(interface_of(id), dec, &value), 0, "%s", vector->name);
                stow_item_release(interface_of(id), &value);
            }
            break;
        case STOW_MSG_ERROR:
        {
            uint32_t failed = 0;
            int err = stow_client_serde_decode_error(dec, &failed);
            zassert_true(err < 0 && err != -ENOMSG, "%s", vector->name);
            break;
        }
        case STOW_MSG_OK:
            zassert_true(zcbor_list_end_decode(dec), "%s", vector->name);
            break;
        default:
            zassert_unreachable("%s: unexpected response %u", vector->name, code);
    }
}

static void decode_server_steps(const struct protocol_vector* vectors, size_t count)
{
    for (size_t v = 0; v < count; v++)
    {
        for (size_t s = 0; s < vectors[v].step_count; s++)
        {
            if (vectors[v].steps[s].side != CLIENT_SIDE)
            {
                decode_response(&vectors[v], s);
            }
        }
    }
}

ZTEST(stow_client_vectors, test_client_decodes_every_response)
{
    for_each_description(bind);
    decode_server_steps(stow_exchanges, stow_exchanges_count);
    decode_server_steps(stow_malformed, stow_malformed_count);
}

static size_t checked_descriptions;

static void check_against_binding(const struct stow_client_serde_description* description)
{
    const struct stow_client_item* item = binding_named(description->name);
    zassert_not_null(item, "no binding for item %u", description->id);
    zassert_equal(stow_client_serde_check(item, description), 0, "%s", item->name);

    if (item->type == STOW_ITEM_TYPE_INT)
    {
        struct stow_client_item narrower = *item;
        narrower.constraints.int_constraints.max--;
        zassert_equal(stow_client_serde_check(&narrower, description), -EINVAL, "%s", item->name);

        struct stow_client_item other_type = *item;
        other_type.type = STOW_ITEM_TYPE_FLOAT;
        other_type.interface = &stow_float_interface;
        other_type.constraints.float_constraints = (struct stow_float_constraints){
            .min = (float)item->constraints.int_constraints.min,
            .max = (float)item->constraints.int_constraints.max,
        };
        zassert_equal(stow_client_serde_check(&other_type, description), -EINVAL, "%s", item->name);
    }
    checked_descriptions++;
}

ZTEST(stow_client_vectors, test_every_described_item_matches_its_binding)
{
    checked_descriptions = 0;
    for_each_description(check_against_binding);
    zassert_true(checked_descriptions > 0, "no step describes an item");
}
