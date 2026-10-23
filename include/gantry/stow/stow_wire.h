/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Stow protocol wire constants and the messages the server and client share
 *
 * @details Independent of any generated schema.
 *
 * @author Demizo (demizodemazo@gmail.com)
 *
 */

#pragma once

#include <gantry/stow/stow_transport.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <zcbor_common.h>
#include <zcbor_encode.h>
#include <zephyr/net_buf.h>

/**
 * @addtogroup stow_protocol
 * @{
 */

//**********************************************************
//* Definitions
//**********************************************************

/**
 * @brief Marks a tag that is absent from a message
 */
#define STOW_TAG_NONE UINT8_MAX

/**
 * @brief Marks an item, index or next item ID that is absent from a message
 */
#define STOW_SERDE_ABSENT UINT32_MAX

//**********************************************************
//* Typedefs, Enums, and Structs
//**********************************************************

/**
 * @brief Contents of an Error response
 */
struct stow_serde_error
{
    enum stow_error_code code; /**< Protocol error code */
    uint8_t tag;               /**< Tag of the failed request, or @ref STOW_TAG_NONE */
    uint32_t item;             /**< Item that failed, or @ref STOW_SERDE_ABSENT */
    uint32_t index;            /**< Zero-based entry that failed as it was applied, or @ref STOW_SERDE_ABSENT */
};

//**********************************************************
//* Functions
//**********************************************************

/**
 * @brief Encode an unsigned integer, or null when it is absent
 *
 * @param encoder CBOR encoder
 * @param value Value to encode, or @ref STOW_SERDE_ABSENT
 *
 * @return true when the value was encoded
 */
static inline bool stow_wire_put_optional_uint(zcbor_state_t* encoder, uint32_t value)
{
    return (value == STOW_SERDE_ABSENT) ? zcbor_nil_put(encoder, NULL) : zcbor_uint32_put(encoder, value);
}

/**
 * @brief Check whether a message code is a request, which only a server answers
 *
 * @details Every code has a fixed direction, so one stream can carry a server and a client session.
 *
 * @param code Message code
 *
 * @return true for Hello, Describe, Get, Set, Subscribe and Unsubscribe
 */
static inline bool stow_wire_is_request(uint32_t code)
{
    return code == STOW_MSG_HELLO || code == STOW_MSG_DESCRIBE || code == STOW_MSG_GET || code == STOW_MSG_SET ||
           code == STOW_MSG_SUBSCRIBE || code == STOW_MSG_UNSUBSCRIBE;
}

/**
 * @brief Encode an Error response
 *
 * @details The item is encoded when present, or as null when only the index is present.
 *
 * @param encoder CBOR encoder instance
 * @param error Error to encode
 *
 * @return SUCCESS when the response was encoded
 * @return -ENOMEM when the encoder lacks room
 */
int stow_wire_encode_error(zcbor_state_t* encoder, const struct stow_serde_error* error);

/**
 * @brief Decode the start of a message: its code and tag
 *
 * @param decoder CBOR decoder over the whole message
 * @param[out] code_out Message code, or @ref STOW_SERDE_ABSENT when it could not be read
 * @param[out] tag_out Tag, or @ref STOW_TAG_NONE when it is null or could not be read
 *
 * @return SUCCESS when the start was decoded
 * @return -EBADMSG when it is malformed or the tag is out of range
 */
int stow_wire_decode_start(zcbor_state_t* decoder, uint32_t* code_out, uint8_t* tag_out);

/**
 * @brief Allocate a message from a transport and start an encoder over it
 *
 * @param transport Transport the message is for
 * @param max_message Largest message the receiver accepts, or 0 for no limit beyond the buffer
 * @param[out] buf_out The message
 * @param encoder Encoder states to start over the message
 * @param states Number of states in @p encoder
 *
 * @return SUCCESS when the message is ready to encode
 * @return -EBUSY when the transport has no free buffer
 * @acquires net_buf buf_out on-success
 */
int stow_wire_message_alloc(
    const struct stow_transport* transport, uint32_t max_message, struct net_buf** buf_out, zcbor_state_t* encoder,
    size_t states);

/**
 * @brief Add what the encoder from @ref stow_wire_message_alloc wrote to the message
 *
 * @param buf The message
 * @param encoder Encoder states started over it
 */
static inline void stow_wire_message_end(struct net_buf* buf, const zcbor_state_t* encoder)
{
    net_buf_add(buf, encoder[0].payload - net_buf_tail(buf));
}

/**
 * @}
 */
