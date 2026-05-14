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
//* Public Function Definitions
//**********************************************************

int stow_serde_decode_request(zcbor_state_t* decoder, struct stow_serde_request* request_out)
{
    int ret = SUCCESS;

    memset(request_out, 0, sizeof(*request_out));

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

    switch (message_code)
    {
        case STOW_MSG_VERSION:
        {
            request_out->message_code = STOW_MSG_VERSION;
            break;
        }
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
                return -EINVAL;
            }
            request_out->message_code = STOW_MSG_DESCRIBE;
            request_out->item_id = (enum stow_item_id)start_id;
            break;
        }
        case STOW_MSG_GET:
        case STOW_MSG_SET:
        case STOW_MSG_SUBSCRIBE:
        case STOW_MSG_UNSUBSCRIBE:
        {
            uint32_t id = 0;
            if (!zcbor_uint32_decode(decoder, &id))
            {
                LOG_WRN("Failed to decode item id for message %u", message_code);
                return -EBADMSG;
            }
            if (!stow_is_id_valid(id))
            {
                LOG_WRN("Invalid item id %u", id);
                return -EINVAL;
            }
            request_out->message_code = (enum stow_message_code)message_code;
            request_out->item_id = (enum stow_item_id)id;

            if (message_code == STOW_MSG_SET)
            {
                data_value_t value = { 0 };
                ret = stow_decode(decoder, request_out->item_id, &value);
                if (ret != SUCCESS)
                {
                    NOT_REFERENCED(value);
                    LOG_WRN("Failed to decode value for item id %u (%d)", id, ret);
                    return ret;
                }
                request_out->value = value;
                request_out->has_value = true;
                PASS_OWNERSHIP(value);
            }
            break;
        }

        case STOW_MSG_MULTI_GET:
        {
            request_out->message_code = STOW_MSG_MULTI_GET;
            while (!zcbor_array_at_end(decoder))
            {
                if (request_out->multi_count >= CONFIG_STOW_PROTOCOL_MULTI_MAX_ITEMS)
                {
                    LOG_WRN("Multi-Get exceeds max items (%u)", CONFIG_STOW_PROTOCOL_MULTI_MAX_ITEMS);
                    return -EINVAL;
                }
                uint32_t id = 0;
                if (!zcbor_uint32_decode(decoder, &id))
                {
                    LOG_WRN("Failed to decode item id in Multi-Get");
                    return -EBADMSG;
                }
                if (!stow_is_id_valid(id))
                {
                    LOG_WRN("Invalid item id %u in Multi-Get", id);
                    return -EINVAL;
                }
                request_out->multi_ids[request_out->multi_count++] = (enum stow_item_id)id;
            }
            break;
        }

        case STOW_MSG_MULTI_SET:
        {
            request_out->message_code = STOW_MSG_MULTI_SET;
            while (!zcbor_array_at_end(decoder))
            {
                if (request_out->multi_count >= CONFIG_STOW_PROTOCOL_MULTI_MAX_ITEMS)
                {
                    LOG_WRN("Multi-Set exceeds max items (%u)", CONFIG_STOW_PROTOCOL_MULTI_MAX_ITEMS);
                    return -EINVAL;
                }
                uint32_t id = 0;
                if (!zcbor_uint32_decode(decoder, &id))
                {
                    LOG_WRN("Failed to decode item id in Multi-Set");
                    return -EBADMSG;
                }
                if (!stow_is_id_valid(id))
                {
                    LOG_WRN("Invalid item id %u in Multi-Set", id);
                    return -EINVAL;
                }
                uint8_t i = request_out->multi_count;
                request_out->multi_ids[i] = (enum stow_item_id)id;

                data_value_t value = { 0 };
                ret = stow_decode(decoder, (enum stow_item_id)id, &value);
                if (ret != SUCCESS)
                {
                    NOT_REFERENCED(value);
                    LOG_WRN("Failed to decode value for item %u in Multi-Set (%d)", id, ret);
                    return ret;
                }
                request_out->multi_values[i] = value;
                request_out->multi_has_value[i] = true;
                request_out->multi_count++;
                PASS_OWNERSHIP(value);
            }
            break;
        }

        default:
            LOG_WRN("Unknown message code %u", message_code);
            return -ENOTSUP;
    }

    if (!zcbor_list_end_decode(decoder))
    {
        LOG_WRN("Message list not closed");
        stow_serde_release_request(request_out);
        return -EBADMSG;
    }

    return SUCCESS;
}

void stow_serde_release_request(struct stow_serde_request* request)
{
    if (request == NULL)
    {
        return;
    }
    if (request->has_value)
    {
        data_value_t value = request->value;
        stow_release(request->item_id, &value);
        request->has_value = false;
    }
    for (uint8_t i = 0; i < request->multi_count; i++)
    {
        if (request->multi_has_value[i])
        {
            data_value_t value = request->multi_values[i];
            stow_release(request->multi_ids[i], &value);
            request->multi_has_value[i] = false;
        }
    }
}

int stow_serde_encode_version_response(zcbor_state_t* encoder)
{
    if (!zcbor_list_start_encode(encoder, 2) || !zcbor_uint32_put(encoder, STOW_MSG_VERSION_RESPONSE) ||
        !zcbor_uint32_put(encoder, STOW_PROTOCOL_VERSION) || !zcbor_list_end_encode(encoder, 2))
    {
        return -ENOMEM;
    }
    return SUCCESS;
}

int stow_serde_encode_describe_response(
    zcbor_state_t* encoder, uint32_t next_item_id, bool has_more, const uint8_t* chunk, size_t chunk_len)
{
    if (!zcbor_list_start_encode(encoder, 4) || !zcbor_uint32_put(encoder, STOW_MSG_DESCRIBE_RESPONSE) ||
        !zcbor_uint32_put(encoder, next_item_id) || !zcbor_bool_put(encoder, has_more) ||
        !zcbor_bstr_encode_ptr(encoder, (const char*)chunk, chunk_len) || !zcbor_list_end_encode(encoder, 4))
    {
        return -ENOMEM;
    }
    return SUCCESS;
}

int stow_serde_encode_get_response(zcbor_state_t* encoder, enum stow_item_id id, data_value_t value)
{
    if (!zcbor_list_start_encode(encoder, 3) || !zcbor_uint32_put(encoder, STOW_MSG_GET_RESPONSE) ||
        !zcbor_uint32_put(encoder, (uint32_t)id))
    {
        return -ENOMEM;
    }

    int ret = stow_encode(encoder, id, value);
    if (ret != SUCCESS)
    {
        return ret;
    }

    if (!zcbor_list_end_encode(encoder, 3))
    {
        return -ENOMEM;
    }
    return SUCCESS;
}

int stow_serde_encode_update(zcbor_state_t* encoder, enum stow_item_id id, data_value_t value)
{
    if (!zcbor_list_start_encode(encoder, 3) || !zcbor_uint32_put(encoder, STOW_MSG_UPDATE) ||
        !zcbor_uint32_put(encoder, (uint32_t)id))
    {
        return -ENOMEM;
    }

    int ret = stow_encode(encoder, id, value);
    if (ret != SUCCESS)
    {
        return ret;
    }

    if (!zcbor_list_end_encode(encoder, 3))
    {
        return -ENOMEM;
    }
    return SUCCESS;
}

int stow_serde_encode_multi_get_response(
    zcbor_state_t* encoder, const enum stow_item_id* ids, const data_value_t* values, uint8_t count)
{
    uint32_t elem_count = 1u + 2u * (uint32_t)count;
    if (!zcbor_list_start_encode(encoder, elem_count) || !zcbor_uint32_put(encoder, STOW_MSG_MULTI_GET_RESPONSE))
    {
        return -ENOMEM;
    }
    for (uint8_t i = 0; i < count; i++)
    {
        if (!zcbor_uint32_put(encoder, (uint32_t)ids[i]))
        {
            return -ENOMEM;
        }
        int ret = stow_encode(encoder, ids[i], values[i]);
        if (ret != SUCCESS)
        {
            return ret;
        }
    }
    if (!zcbor_list_end_encode(encoder, elem_count))
    {
        return -ENOMEM;
    }
    return SUCCESS;
}

int stow_serde_encode_ok(zcbor_state_t* encoder)
{
    if (!zcbor_list_start_encode(encoder, 1) || !zcbor_uint32_put(encoder, STOW_MSG_OK) ||
        !zcbor_list_end_encode(encoder, 1))
    {
        return -ENOMEM;
    }
    return SUCCESS;
}

int stow_serde_encode_error(zcbor_state_t* encoder, enum stow_error_code code)
{
    if (!zcbor_list_start_encode(encoder, 2) || !zcbor_uint32_put(encoder, STOW_MSG_ERROR) ||
        !zcbor_uint32_put(encoder, (uint32_t)code) || !zcbor_list_end_encode(encoder, 2))
    {
        return -ENOMEM;
    }
    return SUCCESS;
}
