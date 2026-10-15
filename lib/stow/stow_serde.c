/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Stow protocol message serialization / deserialization
 *
 * @author Demizo (demizodemazo@gmail.com)
 *
 *
 */

#include <gantry/error.h>
#include <gantry/memory.h>
#include <gantry/stow/stow.h>
#include <gantry/stow/stow_cbor.h>
#include <gantry/stow/stow_serde.h>
#include <generated_stow_items.h>
#include <string.h>
#include <sys/errno.h>
#include <zcbor_decode.h>
#include <zcbor_encode.h>
#include <zephyr/logging/log.h>

/**
 * @brief Logger for module
 */
LOG_MODULE_REGISTER(stow_serde, CONFIG_STOW_SERDE_LOG_LEVEL);

//**********************************************************
//* Local Definitions
//**********************************************************

//**********************************************************
//* Static Function Declarations
//**********************************************************

static int check_value(zcbor_state_t* decoder, uint32_t id);
static int check_entries(zcbor_state_t* decoder, struct stow_serde_request* request);
static bool put_optional_uint(zcbor_state_t* encoder, uint32_t value);

//**********************************************************
//* Static Function Definitions
//**********************************************************

/**
 * @brief Check that a Set value is well formed without keeping it
 *
 * @param decoder CBOR decoder positioned at the value
 * @param id Item the entry names
 *
 * @return SUCCESS when the value is well formed
 * @return -EBADMSG when it is not, or does not decode as a known item's value
 * @return -ENOMEM when memory cannot be allocated for the decoded value
 */
static int check_value(zcbor_state_t* decoder, uint32_t id)
{
    if (!stow_is_id_valid(id))
    {
        return zcbor_any_skip(decoder, NULL) ? SUCCESS : -EBADMSG;
    }

    data_value_t value = { 0 };
    int ret = stow_decode(decoder, (enum stow_item_id)id, &value);
    if (ret != SUCCESS)
    {
        return ret;
    }
    stow_release((enum stow_item_id)id, &value);
    return SUCCESS;
}

/**
 * @brief Check every entry of a request without applying or keeping any
 *
 * @param decoder CBOR decoder positioned at the first entry
 * @param[in,out] request Request whose entries are counted
 *
 * @return SUCCESS when every entry is well formed
 * @return -EBADMSG when an entry is malformed or there are none
 * @return -ENOMEM when memory cannot be allocated for a decoded value
 */
static int check_entries(zcbor_state_t* decoder, struct stow_serde_request* request)
{
    while (!zcbor_array_at_end(decoder))
    {
        uint32_t id = 0;
        if (!zcbor_uint32_decode(decoder, &id) || request->entry_count == UINT16_MAX)
        {
            LOG_WRN("Failed to decode item id in message %u", request->message_code);
            return -EBADMSG;
        }

        if (request->message_code == STOW_MSG_SET)
        {
            int ret = check_value(decoder, id);
            if (ret != SUCCESS)
            {
                LOG_WRN("Failed to decode value for item %u (%d)", id, ret);
                request->error_item = id;
                return ret;
            }
        }
        request->entry_count++;
    }

    if (request->entry_count == 0)
    {
        LOG_WRN("Message %u names no items", request->message_code);
        return -EBADMSG;
    }
    return SUCCESS;
}

/**
 * @brief Encode an unsigned integer, or null when it is absent
 *
 * @param encoder CBOR encoder instance
 * @param value Value to encode, or @ref STOW_SERDE_ABSENT
 *
 * @return true when the value was encoded
 */
static bool put_optional_uint(zcbor_state_t* encoder, uint32_t value)
{
    return (value == STOW_SERDE_ABSENT) ? zcbor_nil_put(encoder, NULL) : zcbor_uint32_put(encoder, value);
}

//**********************************************************
//* Public Function Definitions
//**********************************************************

int stow_serde_decode_request(zcbor_state_t* decoder, struct stow_serde_request* request_out)
{
    memset(request_out, 0, sizeof(*request_out));
    request_out->tag = STOW_TAG_NONE;
    request_out->error_item = STOW_SERDE_ABSENT;
    request_out->payload = decoder->payload;
    request_out->payload_len = (size_t)(decoder->payload_end - decoder->payload);

    if (!zcbor_list_start_decode(decoder))
    {
        LOG_WRN("No list start");
        return -EBADMSG;
    }

    uint32_t message_code = 0;
    if (!zcbor_uint32_decode(decoder, &message_code))
    {
        LOG_WRN("Failed to decode message code");
        return -EBADMSG;
    }

    uint32_t tag = 0;
    if (!zcbor_uint32_decode(decoder, &tag) || tag > STOW_TAG_MAX)
    {
        LOG_WRN("Failed to decode tag for message %u", message_code);
        return -EBADMSG;
    }
    request_out->tag = (uint8_t)tag;
    request_out->message_code = (enum stow_message_code)message_code;

    int ret = SUCCESS;
    switch (message_code)
    {
        case STOW_MSG_HELLO:
            break;
        case STOW_MSG_DESCRIBE:
        {
            uint32_t start_id = 0;
            if (!zcbor_uint32_decode(decoder, &start_id))
            {
                LOG_WRN("Failed to decode start id for describe");
                return -EBADMSG;
            }
            if (start_id >= STOW_ID_COUNT)
            {
                LOG_WRN("Invalid start id %u for describe", start_id);
                request_out->error_item = start_id;
                return -ENOENT;
            }
            request_out->item_id = (enum stow_item_id)start_id;
            break;
        }
        case STOW_MSG_GET:
        case STOW_MSG_SET:
        case STOW_MSG_SUBSCRIBE:
        case STOW_MSG_UNSUBSCRIBE:
            ret = check_entries(decoder, request_out);
            break;
        default:
            LOG_WRN("Unknown message code %u", message_code);
            return -ENOTSUP;
    }
    if (ret != SUCCESS)
    {
        return ret;
    }

    if (!zcbor_list_end_decode(decoder))
    {
        LOG_WRN("Message list not closed");
        return -EBADMSG;
    }

    return SUCCESS;
}

void stow_serde_entries_start(const struct stow_serde_request* request, zcbor_state_t* decoder, size_t states)
{
    zcbor_new_decode_state(decoder, states, request->payload, request->payload_len, 1, NULL, 0);
    uint32_t header = 0;
    bool ok = zcbor_list_start_decode(decoder) && zcbor_uint32_decode(decoder, &header) &&
              zcbor_uint32_decode(decoder, &header);
    ASSERT(ok, "Request changed after it was checked");
}

int stow_serde_decode_entry(zcbor_state_t* decoder, uint32_t* id_out)
{
    bool ok = zcbor_uint32_decode(decoder, id_out);
    ASSERT(ok, "Request changed after it was checked");
    return stow_is_id_valid(*id_out) ? SUCCESS : -ENOENT;
}

int stow_serde_encode_hello_response(zcbor_state_t* encoder, uint8_t tag, const struct stow_serde_hello* hello)
{
    struct zcbor_string hash = { .value = (const uint8_t*)hello->stow_hash, .len = strlen(hello->stow_hash) };

    if (!stow_cbor_list_start(encoder, 4) || !zcbor_uint32_put(encoder, STOW_MSG_HELLO_RESPONSE) ||
        !zcbor_uint32_put(encoder, tag) || !zcbor_uint32_put(encoder, STOW_PROTOCOL_VERSION) ||
        !stow_cbor_map_start(encoder, 4) || !zcbor_uint32_put(encoder, STOW_HELLO_MAX_MESSAGE) ||
        !zcbor_uint32_put(encoder, hello->max_message) || !zcbor_uint32_put(encoder, STOW_HELLO_STOW_HASH) ||
        !zcbor_tstr_encode(encoder, &hash) || !zcbor_uint32_put(encoder, STOW_HELLO_ROLES) ||
        !zcbor_uint32_put(encoder, hello->roles) || !zcbor_uint32_put(encoder, STOW_HELLO_ROLE_NAMES) ||
        !stow_cbor_list_start(encoder, STOW_ROLE_COUNT))
    {
        return -ENOMEM;
    }

    for (int bit = 0; bit < STOW_ROLE_COUNT; bit++)
    {
        struct zcbor_string name = {
            .value = (const uint8_t*)g_stow_role_names[bit],
            .len = strlen(g_stow_role_names[bit]),
        };
        if (!zcbor_tstr_encode(encoder, &name))
        {
            return -ENOMEM;
        }
    }

    return SUCCESS;
}

int stow_serde_encode_describe_response(
    zcbor_state_t* encoder, uint8_t tag, uint32_t next_item_id, bool has_more, const uint8_t* chunk, size_t chunk_len)
{
    if (!stow_cbor_list_start(encoder, 5) || !zcbor_uint32_put(encoder, STOW_MSG_DESCRIBE_RESPONSE) ||
        !zcbor_uint32_put(encoder, tag) || !zcbor_uint32_put(encoder, next_item_id) ||
        !zcbor_bool_put(encoder, has_more) || !zcbor_bstr_encode_ptr(encoder, (const char*)chunk, chunk_len))
    {
        return -ENOMEM;
    }
    return SUCCESS;
}

int stow_serde_encode_get_start(zcbor_state_t* encoder, uint8_t tag, uint16_t count)
{
    uint32_t elem_count = 2u + 2u * (uint32_t)count;
    if (!stow_cbor_list_start(encoder, elem_count) || !zcbor_uint32_put(encoder, STOW_MSG_GET_RESPONSE) ||
        !zcbor_uint32_put(encoder, tag))
    {
        return -ENOMEM;
    }
    return SUCCESS;
}

int stow_serde_encode_get_entry(zcbor_state_t* encoder, enum stow_item_id id, data_value_t value)
{
    if (!zcbor_uint32_put(encoder, (uint32_t)id))
    {
        return -ENOMEM;
    }
    return stow_encode(encoder, id, value);
}

int stow_serde_encode_update(zcbor_state_t* encoder, enum stow_item_id id, data_value_t value)
{
    if (!stow_cbor_list_start(encoder, 4) || !zcbor_uint32_put(encoder, STOW_MSG_UPDATE) ||
        !zcbor_nil_put(encoder, NULL) || !zcbor_uint32_put(encoder, (uint32_t)id))
    {
        return -ENOMEM;
    }
    return stow_encode(encoder, id, value);
}

int stow_serde_encode_ok(zcbor_state_t* encoder, uint8_t tag)
{
    if (!stow_cbor_list_start(encoder, 2) || !zcbor_uint32_put(encoder, STOW_MSG_OK) || !zcbor_uint32_put(encoder, tag))
    {
        return -ENOMEM;
    }
    return SUCCESS;
}

int stow_serde_encode_error(zcbor_state_t* encoder, const struct stow_serde_error* error)
{
    bool has_index = (error->index != STOW_SERDE_ABSENT);
    bool has_item = has_index || (error->item != STOW_SERDE_ABSENT);
    uint32_t elem_count = 3u + (has_item ? 1u : 0u) + (has_index ? 1u : 0u);

    if (!stow_cbor_list_start(encoder, elem_count) || !zcbor_uint32_put(encoder, STOW_MSG_ERROR) ||
        !(error->tag == STOW_TAG_NONE ? zcbor_nil_put(encoder, NULL) : zcbor_uint32_put(encoder, error->tag)) ||
        !zcbor_uint32_put(encoder, (uint32_t)error->code) || (has_item && !put_optional_uint(encoder, error->item)) ||
        (has_index && !zcbor_uint32_put(encoder, error->index)))
    {
        return -ENOMEM;
    }
    return SUCCESS;
}
