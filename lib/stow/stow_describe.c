/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 *
 * @brief Chunked CBOR encoding of stow item metadata
 *
 * @author Demizo (demizodemazo@gmail.com)
 *
 *
 */

#include <gantry/error.h>
#include <gantry/stow/stow_cbor.h>
#include <gantry/stow/stow_describe.h>
#include <gantry/stow/types/stow_types.h>
#include <generated_stow_items.h>
#include <string.h>
#include <sys/errno.h>
#include <zcbor_encode.h>
#include <zephyr/logging/log.h>

/**
 * @brief Logger for module
 */
LOG_MODULE_REGISTER(stow_describe, CONFIG_STOW_LOG_LEVEL);

/**
 * @brief String representation of each storage type
 */
static const char* const storage_type_strings[] = {
    [STOW_STORAGE_EPHEMERAL] = "Ephemeral",
    [STOW_STORAGE_PERSISTENT] = "Persistent",
    [STOW_STORAGE_TOFU] = "TOFU",
};

/**
 * @brief String representation of each item type
 */
const char* const item_type_strings[] = {
    [STOW_ITEM_TYPE_ENUM] = "Enum",
    [STOW_ITEM_TYPE_INT] = "Int",
    [STOW_ITEM_TYPE_FLOAT] = "Float",
    [STOW_ITEM_TYPE_STRING] = "String",
    [STOW_ITEM_TYPE_BYTE_ARRAY] = "Byte Array",
    [STOW_ITEM_TYPE_BUFFER] = "Buffer",
    [STOW_ITEM_TYPE_STRUCT] = "Struct",
};

//**********************************************************
//* Static Function Declarations
//**********************************************************

static int encode_item(zcbor_state_t* encoder, const struct stow_item_const_metadata* item);
static uint32_t next_described(const atomic_t* filter, uint32_t id);
static bool tstr_put(zcbor_state_t* encoder, const char* str);
static int encode_role_list(zcbor_state_t* encoder, stow_role_t perm);

//**********************************************************
//* Static Function Definitions
//**********************************************************

/**
 * @brief Find the next item to describe
 *
 * @param filter Bitmap of item IDs to describe, or NULL for every item
 * @param id First item ID to consider
 *
 * @return The first described item ID at or after @p id, or STOW_ID_COUNT when there is none
 */
static uint32_t next_described(const atomic_t* filter, uint32_t id)
{
    while (id < STOW_ID_COUNT && filter != NULL && !atomic_test_bit(filter, (int)id))
    {
        id++;
    }
    return id;
}

static bool tstr_put(zcbor_state_t* encoder, const char* str)
{
    struct zcbor_string s = { .value = str, .len = strlen(str) };
    return zcbor_tstr_encode(encoder, &s);
}

static int encode_role_list(zcbor_state_t* encoder, stow_role_t perm)
{
    uint8_t count = 0;
    for (uint8_t bit = 0; bit < STOW_ROLE_COUNT; bit++)
    {
        if (perm & (stow_role_t)(1U << bit))
        {
            count++;
        }
    }
    if (!stow_cbor_list_start(encoder, count))
    {
        return -ENOMEM;
    }
    for (uint8_t bit = 0; bit < STOW_ROLE_COUNT; bit++)
    {
        if (perm & (stow_role_t)(1U << bit))
        {
            if (!tstr_put(encoder, g_stow_role_names[bit]))
            {
                return -ENOMEM;
            }
        }
    }
    return SUCCESS;
}

/**
 * @brief Encode the provided item's metadata
 *
 * @param encoder Encoder to populate with the metadata
 * @param item The item to describe
 *
 * @return SUCCESS when the item metadata was fully encoded
 * @return -ENOMEM when there is no more room in the encoder
 */
static int encode_item(zcbor_state_t* encoder, const struct stow_item_const_metadata* item)
{
    data_value_t default_value = {
        .type = item->type,
        .data = item->default_value,
    };

    struct zcbor_string name_str = {
        .value = item->name,
        .len = strlen(item->name),
    };

    if (!stow_cbor_map_start(encoder, 9) || !zcbor_tstr_put_lit(encoder, "id") ||
        !zcbor_uint32_put(encoder, item->id) || !zcbor_tstr_put_lit(encoder, "name") ||
        !zcbor_tstr_encode(encoder, &name_str))
    {
        return -ENOMEM;
    }

    if (!zcbor_tstr_put_lit(encoder, "categories") || !stow_cbor_list_start(encoder, item->category_count))
    {
        return -ENOMEM;
    }
    for (uint8_t i = 0; i < item->category_count; i++)
    {
        struct zcbor_string category_str = {
            .value = item->categories[i],
            .len = strlen(item->categories[i]),
        };
        if (!zcbor_tstr_encode(encoder, &category_str))
        {
            return -ENOMEM;
        }
    }

    if (!zcbor_tstr_put_lit(encoder, "storage") || !tstr_put(encoder, storage_type_strings[item->storage_type]) ||
        !zcbor_tstr_put_lit(encoder, "read_perm") ||
        encode_role_list(encoder, item->permissions.read_permissions) != SUCCESS ||
        !zcbor_tstr_put_lit(encoder, "write_perm") ||
        encode_role_list(encoder, item->permissions.write_permissions) != SUCCESS ||
        !zcbor_tstr_put_lit(encoder, "type") || !tstr_put(encoder, item_type_strings[item->type]))
    {
        return -ENOMEM;
    }

    if (!zcbor_tstr_put_lit(encoder, "default"))
    {
        return -ENOMEM;
    }

    int ret = item->interface->encode(encoder, default_value);
    if (ret != SUCCESS)
    {
        return ret;
    }

    if (!zcbor_tstr_put_lit(encoder, "constraints"))
    {
        return -ENOMEM;
    }

    ret = item->interface->encode_constraints(encoder, &item->constraints);
    if (ret != SUCCESS)
    {
        return ret;
    }

    return SUCCESS;
}

//**********************************************************
//* Public Function Definitions
//**********************************************************

int stow_describe(uint32_t start_id, const atomic_t* filter, zcbor_state_t* encoder, uint32_t* next_id_out)
{
    uint32_t id = next_described(filter, start_id);
    bool encoded_any = false;

    while (id < STOW_ID_COUNT)
    {
        zcbor_state_t saved = *encoder;
        int ret = encode_item(encoder, &g_stow_const_metadata[id]);
        if (ret != SUCCESS)
        {
            *encoder = saved;
            *next_id_out = id;
            return encoded_any ? ret : -E2BIG;
        }

        encoded_any = true;
        id = next_described(filter, id + 1);
    }

    *next_id_out = STOW_ID_COUNT;
    return SUCCESS;
}
