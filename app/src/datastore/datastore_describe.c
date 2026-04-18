/**
 * @file datastore_describe.c
 *
 * @brief Chunked CBOR encoding of datastore item metadata
 *
 * @author Demizo (demizodemazo@gmail.com)
 * @version 0.1
 * @date 2026-04-17
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "datastore_describe.h"

#include <generated_datastore_items.h>
#include <string.h>
#include <sys/errno.h>
#include <zcbor_encode.h>
#include <zephyr/logging/log.h>

#include "datastore_types.h"
#include "error.h"

/**
 * @brief Logger for module
 */
LOG_MODULE_REGISTER(datastore_describe, CONFIG_DATASTORE_LOG_LEVEL);

//**********************************************************
//* Static Function Declarations
//**********************************************************

static int encode_item(zcbor_state_t* encoder, const struct datastore_item_const_metadata* item);

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
static int encode_item(zcbor_state_t* encoder, const struct datastore_item_const_metadata* item)
{
    data_value_t default_value = {
        .type = item->type,
        .data = item->default_value,
    };

    struct zcbor_string name_str = {
        .value = item->name,
        .len = strlen(item->name),
    };

    if (!zcbor_map_start_encode(encoder, 8) || !zcbor_tstr_put_lit(encoder, "id") ||
        !zcbor_uint32_put(encoder, item->id) || !zcbor_tstr_put_lit(encoder, "name") ||
        !zcbor_tstr_encode(encoder, &name_str) || !zcbor_tstr_put_lit(encoder, "storage") ||
        !zcbor_uint32_put(encoder, (uint32_t)item->storage_type) || !zcbor_tstr_put_lit(encoder, "read_perm") ||
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

    ret = item->interface->encode_constraints(encoder, item);
    if (ret != SUCCESS)
    {
        return ret;
    }

    if (!zcbor_map_end_encode(encoder, 8))
    {
        return -ENOMEM;
    }

    return SUCCESS;
}

//**********************************************************
//* Public Function Definitions
//**********************************************************

void datastore_describe_start(struct datastore_describe_state* describe_state) { describe_state->current_id = 0; }

int datastore_describe(struct datastore_describe_state* describe_state, zcbor_state_t* encoder)
{
    while (describe_state->current_id < DATASTORE_ID_COUNT)
    {
        const struct datastore_item_const_metadata* item = &g_datastore_const_metadata[describe_state->current_id];

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
