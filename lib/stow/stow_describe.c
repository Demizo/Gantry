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

#include <generated_stow_items.h>
#include <string.h>
#include <sys/errno.h>
#include <zcbor_encode.h>
#include <zds/error.h>
#include <zds/stow/stow_describe.h>
#include <zds/stow/types/stow_types.h>
#include <zephyr/logging/log.h>

/**
 * @brief Logger for module
 */
LOG_MODULE_REGISTER(stow_describe, CONFIG_STOW_LOG_LEVEL);

//**********************************************************
//* Static Function Declarations
//**********************************************************

static int encode_item(zcbor_state_t* encoder, const struct stow_item_const_metadata* item);

//**********************************************************
//* Static Function Definitions
//**********************************************************

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

    if (!zcbor_map_start_encode(encoder, 9) || !zcbor_tstr_put_lit(encoder, "id") ||
        !zcbor_uint32_put(encoder, item->id) || !zcbor_tstr_put_lit(encoder, "name") ||
        !zcbor_tstr_encode(encoder, &name_str))
    {
        return -ENOMEM;
    }

    if (!zcbor_tstr_put_lit(encoder, "categories") || !zcbor_list_start_encode(encoder, item->category_count))
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
    if (!zcbor_list_end_encode(encoder, item->category_count))
    {
        return -ENOMEM;
    }

    if (!zcbor_tstr_put_lit(encoder, "storage") || !zcbor_uint32_put(encoder, (uint32_t)item->storage_type) ||
        !zcbor_tstr_put_lit(encoder, "read_perm") ||
        !zcbor_uint32_put(encoder, (uint32_t)item->permissions.read_permissions) ||
        !zcbor_tstr_put_lit(encoder, "write_perm") ||
        !zcbor_uint32_put(encoder, (uint32_t)item->permissions.write_permissions) ||
        !zcbor_tstr_put_lit(encoder, "type") || !zcbor_uint32_put(encoder, (uint32_t)item->type))
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

    if (!zcbor_map_end_encode(encoder, 9))
    {
        return -ENOMEM;
    }

    return SUCCESS;
}

//**********************************************************
//* Public Function Definitions
//**********************************************************

void stow_describe_start(struct stow_describe_state* describe_state) { describe_state->current_id = 0; }

int stow_describe(struct stow_describe_state* describe_state, zcbor_state_t* encoder)
{
    while (describe_state->current_id < STOW_ID_COUNT)
    {
        const struct stow_item_const_metadata* item = &g_stow_const_metadata[describe_state->current_id];

        zcbor_state_t saved = *encoder;

        int ret = encode_item(encoder, item);
        if (ret != SUCCESS)
        {
            *encoder = saved;
            return ret;
        }

        describe_state->current_id++;
    }

    return SUCCESS;
}
