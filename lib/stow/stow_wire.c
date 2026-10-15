/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Stow protocol messages shared by the server and the client
 *
 * @author Demizo (demizodemazo@gmail.com)
 *
 */

#include <gantry/error.h>
#include <gantry/stow/stow_cbor.h>
#include <gantry/stow/stow_wire.h>
#include <sys/errno.h>
#include <zcbor_encode.h>

//**********************************************************
//* Public Function Definitions
//**********************************************************

int stow_wire_encode_error(zcbor_state_t* encoder, const struct stow_serde_error* error)
{
    bool has_index = (error->index != STOW_SERDE_ABSENT);
    bool has_item = has_index || (error->item != STOW_SERDE_ABSENT);
    uint32_t elem_count = 3u + (has_item ? 1u : 0u) + (has_index ? 1u : 0u);

    if (!stow_cbor_list_start(encoder, elem_count) || !zcbor_uint32_put(encoder, STOW_MSG_ERROR) ||
        !(error->tag == STOW_TAG_NONE ? zcbor_nil_put(encoder, NULL) : zcbor_uint32_put(encoder, error->tag)) ||
        !zcbor_uint32_put(encoder, (uint32_t)error->code) ||
        (has_item && !stow_wire_put_optional_uint(encoder, error->item)) ||
        (has_index && !zcbor_uint32_put(encoder, error->index)))
    {
        return -ENOMEM;
    }
    return SUCCESS;
}
