/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Stow client request encoding and response decoding
 *
 * @author Demizo (demizodemazo@gmail.com)
 *
 */

#include <gantry/error.h>
#include <gantry/memory.h>
#include <gantry/stow/stow_cbor.h>
#include <gantry/stow/stow_client_serde.h>
#include <gantry/stow/stow_describe.h>
#include <gantry/stow/stow_wire.h>
#include <string.h>
#include <sys/errno.h>
#include <zcbor_decode.h>
#include <zcbor_encode.h>

//**********************************************************
//* Local Definitions
//**********************************************************

/**
 * @brief Number of zcbor encoder state slots
 */
#define ZCBOR_STATES 4

//**********************************************************
//* Static Function Declarations
//**********************************************************

static size_t uint_size(uint32_t value);
static bool decode_optional_uint(zcbor_state_t* decoder, uint32_t* value_out);

//**********************************************************
//* Static Function Definitions
//**********************************************************

/**
 * @brief Size of an unsigned integer or a container or string header in CBOR
 *
 * @param value The integer, count or length
 *
 * @return Encoded size in bytes
 */
static size_t uint_size(uint32_t value)
{
    if (value < 24U)
    {
        return 1U;
    }
    if (value <= UINT8_MAX)
    {
        return 2U;
    }
    return (value <= UINT16_MAX) ? 3U : 5U;
}

/**
 * @brief Decode an unsigned integer that may be null
 *
 * @param decoder CBOR decoder
 * @param[out] value_out The integer, or @ref STOW_SERDE_ABSENT for null
 *
 * @return true when the element was decoded
 */
static bool decode_optional_uint(zcbor_state_t* decoder, uint32_t* value_out)
{
    if (zcbor_nil_expect(decoder, NULL))
    {
        *value_out = STOW_SERDE_ABSENT;
        return true;
    }
    return zcbor_uint32_decode(decoder, value_out);
}

//**********************************************************
//* Public Function Definitions
//**********************************************************

int stow_client_serde_encode_start(zcbor_state_t* encoder, enum stow_message_code code, uint8_t tag, uint32_t count)
{
    if (!stow_cbor_list_start(encoder, 2U + count) || !zcbor_uint32_put(encoder, code) ||
        !zcbor_uint32_put(encoder, tag))
    {
        return -ENOMEM;
    }
    return SUCCESS;
}

int stow_client_serde_encode_id(zcbor_state_t* encoder, uint32_t id)
{
    return zcbor_uint32_put(encoder, id) ? SUCCESS : -ENOMEM;
}

int stow_client_serde_encode_describe(
    zcbor_state_t* encoder, uint8_t tag, uint32_t start_id, const struct stow_client_item* items, size_t count)
{
    if (stow_client_serde_encode_start(encoder, STOW_MSG_DESCRIBE, tag, 2U) != SUCCESS ||
        !zcbor_uint32_put(encoder, start_id) || !stow_cbor_list_start(encoder, count))
    {
        return -ENOMEM;
    }

    for (size_t i = 0; i < count; i++)
    {
        struct zcbor_string name = { .value = (const uint8_t*)items[i].name, .len = strlen(items[i].name) };
        if (!zcbor_tstr_encode(encoder, &name))
        {
            return -ENOMEM;
        }
    }
    return SUCCESS;
}

size_t stow_client_serde_describe_fit(
    size_t budget, uint32_t start_id, const struct stow_client_item* items, size_t count)
{
    size_t names_len = 0;
    size_t fit = 0;
    for (size_t i = 0; i < count; i++)
    {
        size_t len = strlen(items[i].name);
        names_len += uint_size((uint32_t)len) + len;

        size_t total = 3U + uint_size(start_id) + uint_size((uint32_t)(i + 1U)) + names_len;
        if (total > budget)
        {
            break;
        }
        fit = i + 1U;
    }
    return fit;
}

int stow_client_serde_decode_hello(zcbor_state_t* decoder, struct stow_client_serde_hello* hello_out)
{
    uint32_t version = 0;
    if (!zcbor_uint32_decode(decoder, &version))
    {
        return -EBADMSG;
    }
    if (version != STOW_PROTOCOL_VERSION)
    {
        return -EPROTONOSUPPORT;
    }

    bool has_max_message = false;
    hello_out->roles = 0;
    if (!zcbor_map_start_decode(decoder))
    {
        return -EBADMSG;
    }
    while (!zcbor_array_at_end(decoder))
    {
        uint32_t key = 0;
        uint32_t value = 0;
        if (!zcbor_uint32_decode(decoder, &key))
        {
            return -EBADMSG;
        }
        switch (key)
        {
            case STOW_HELLO_MAX_MESSAGE:
                has_max_message = zcbor_uint32_decode(decoder, &hello_out->max_message);
                if (!has_max_message)
                {
                    return -EBADMSG;
                }
                break;
            case STOW_HELLO_ROLES:
                if (!zcbor_uint32_decode(decoder, &value) || value > UINT16_MAX)
                {
                    return -EBADMSG;
                }
                hello_out->roles = (stow_role_t)value;
                break;
            default:
                if (!zcbor_any_skip(decoder, NULL))
                {
                    return -EBADMSG;
                }
                break;
        }
    }
    if (!zcbor_map_end_decode(decoder) || !has_max_message || hello_out->max_message == 0U)
    {
        return -EBADMSG;
    }
    return SUCCESS;
}

int stow_client_serde_decode_describe(zcbor_state_t* decoder, uint32_t* next_id_out, struct zcbor_string* chunk_out)
{
    if (!decode_optional_uint(decoder, next_id_out) || !zcbor_bstr_decode(decoder, chunk_out))
    {
        return -EBADMSG;
    }
    return SUCCESS;
}

int stow_client_serde_decode_description(zcbor_state_t* decoder, struct stow_client_serde_description* description_out)
{
    enum
    {
        SEEN_ID = BIT(0),
        SEEN_NAME = BIT(1),
        SEEN_TYPE = BIT(2),
        SEEN_CONSTRAINTS = BIT(3),
        SEEN_ALL = SEEN_ID | SEEN_NAME | SEEN_TYPE | SEEN_CONSTRAINTS,
    };
    uint32_t seen = 0;

    if (!zcbor_map_start_decode(decoder))
    {
        return -EBADMSG;
    }
    while (!zcbor_array_at_end(decoder))
    {
        uint32_t key = 0;
        if (!zcbor_uint32_decode(decoder, &key))
        {
            return -EBADMSG;
        }

        bool ok = true;
        const uint8_t* start = decoder->payload;
        switch (key)
        {
            case STOW_DESCRIBE_ID:
                ok = zcbor_uint32_decode(decoder, &description_out->id);
                seen |= SEEN_ID;
                break;
            case STOW_DESCRIBE_NAME:
                ok = zcbor_tstr_decode(decoder, &description_out->name);
                seen |= SEEN_NAME;
                break;
            case STOW_DESCRIBE_TYPE:
                ok = zcbor_uint32_decode(decoder, &description_out->type);
                seen |= SEEN_TYPE;
                break;
            case STOW_DESCRIBE_CONSTRAINTS:
                ok = zcbor_any_skip(decoder, NULL);
                description_out->constraints.value = start;
                description_out->constraints.len = (size_t)(decoder->payload - start);
                seen |= SEEN_CONSTRAINTS;
                break;
            default:
                ok = zcbor_any_skip(decoder, NULL);
                break;
        }
        if (!ok)
        {
            return -EBADMSG;
        }
    }
    if (!zcbor_map_end_decode(decoder) || seen != SEEN_ALL)
    {
        return -EBADMSG;
    }
    return SUCCESS;
}

int stow_client_serde_check(
    const struct stow_client_item* item, const struct stow_client_serde_description* description)
{
    if (description->type != (uint32_t)item->type)
    {
        return -EINVAL;
    }

    size_t len = description->constraints.len;
    void* expected = NULL;
    int ret = MEM_ALLOC(len, &expected);
    if (ret != SUCCESS)
    {
        return -ENOMEM;
    }

    zcbor_state_t encoder[ZCBOR_STATES];
    zcbor_new_encode_state(encoder, ARRAY_SIZE(encoder), expected, len, 1);
    ret = item->interface->encode_constraints(encoder, &item->constraints);
    if (ret == SUCCESS && ((size_t)(encoder[0].payload - (const uint8_t*)expected) != len ||
                           memcmp(expected, description->constraints.value, len) != 0))
    {
        ret = -EINVAL;
    }
    MEM_UNREF(&expected);
    return (ret == -ENOMEM) ? -EINVAL : ret;
}

int stow_client_serde_decode_error(zcbor_state_t* decoder, uint32_t* index_out)
{
    uint32_t code = 0;
    uint32_t item = 0;
    *index_out = STOW_SERDE_ABSENT;

    if (!zcbor_uint32_decode(decoder, &code) ||
        (!zcbor_array_at_end(decoder) && !decode_optional_uint(decoder, &item)) ||
        (!zcbor_array_at_end(decoder) && !zcbor_uint32_decode(decoder, index_out)) || !zcbor_list_end_decode(decoder))
    {
        *index_out = STOW_SERDE_ABSENT;
        return -ENOMSG;
    }

    switch ((enum stow_error_code)code)
    {
        case STOW_ERR_MALFORMED_MSG:
            return -EBADMSG;
        case STOW_ERR_UNKNOWN_MSG:
            return -EPROTO;
        case STOW_ERR_INVALID_ITEM:
            return -ENOENT;
        case STOW_ERR_OUT_OF_MEMORY:
            return -ENOMEM;
        case STOW_ERR_PERMISSION_DENIED:
            return -EACCES;
        case STOW_ERR_INVALID_VALUE:
            return -EINVAL;
        case STOW_ERR_ALREADY_SET:
            return -EALREADY;
        case STOW_ERR_TOO_LARGE:
            return -E2BIG;
        default:
            return -EIO;
    }
}
