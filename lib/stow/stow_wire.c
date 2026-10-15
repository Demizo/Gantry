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
#include <gantry/memory.h>
#include <gantry/stow/stow_cbor.h>
#include <gantry/stow/stow_wire.h>
#include <sys/errno.h>
#include <zcbor_decode.h>
#include <zcbor_encode.h>
#include <zephyr/kernel.h>
#include <zephyr/net_buf.h>

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

int stow_wire_decode_start(zcbor_state_t* decoder, uint32_t* code_out, uint8_t* tag_out)
{
    uint32_t tag = STOW_SERDE_ABSENT;
    *code_out = STOW_SERDE_ABSENT;
    *tag_out = STOW_TAG_NONE;
    if (!zcbor_list_start_decode(decoder) || !zcbor_uint32_decode(decoder, code_out) ||
        !(zcbor_nil_expect(decoder, NULL) || zcbor_uint32_decode(decoder, &tag)) ||
        (tag != STOW_SERDE_ABSENT && tag > STOW_TAG_MAX))
    {
        return -EBADMSG;
    }
    *tag_out = (tag == STOW_SERDE_ABSENT) ? STOW_TAG_NONE : (uint8_t)tag;
    return SUCCESS;
}

int stow_wire_message_alloc(
    const struct stow_transport* transport, uint32_t max_message, struct net_buf** buf_out, zcbor_state_t* encoder,
    size_t states)
{
    struct net_buf* buf = net_buf_alloc(transport->pool, K_NO_WAIT);
    if (buf == NULL)
    {
        return -EBUSY;
    }
    net_buf_reserve(buf, transport->headroom);

    size_t budget = net_buf_tailroom(buf) - transport->tailroom;
    if (max_message != 0U && max_message < budget)
    {
        budget = max_message;
    }
    zcbor_new_encode_state(encoder, states, net_buf_tail(buf), budget, 1);

    *buf_out = buf;
    PASS_OWNERSHIP(buf);
    return SUCCESS;
}
