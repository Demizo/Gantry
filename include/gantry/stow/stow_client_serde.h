/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Encoding of Stow client requests and decoding of their responses
 *
 * @details Stateless, and independent of the local Stow schema. Item IDs are the peer's.
 *
 * @author Demizo (demizodemazo@gmail.com)
 *
 */

#pragma once

#include <gantry/stow/stow_client.h>
#include <gantry/stow/stow_wire.h>
#include <stddef.h>
#include <stdint.h>
#include <zcbor_common.h>

/**
 * @addtogroup stow_client
 * @{
 */

//**********************************************************
//* Typedefs, Enums, and Structs
//**********************************************************

/**
 * @brief Contents of a Hello Response the client uses
 */
struct stow_client_serde_hello
{
    uint32_t max_message; /**< Largest message the server can send, in bytes */
    stow_role_t roles;    /**< Roles the session holds */
};

/**
 * @brief Parts of one item description the client checks
 */
struct stow_client_serde_description
{
    uint32_t id;                     /**< The peer's ID for the item */
    struct zcbor_string name;        /**< Item name */
    uint32_t type;                   /**< Type code */
    struct zcbor_string constraints; /**< The encoded constraints */
};

//**********************************************************
//* Functions
//**********************************************************

/**
 * @brief Encode the start of a request
 *
 * @details Follow it with @p count elements.
 *
 * @param encoder CBOR encoder
 * @param code Message code
 * @param tag Request tag
 * @param count Number of elements after the tag
 *
 * @return SUCCESS when the start was encoded
 * @return -ENOMEM when the encoder lacks room
 */
int stow_client_serde_encode_start(zcbor_state_t* encoder, enum stow_message_code code, uint8_t tag, uint32_t count);

/**
 * @brief Encode one item ID of a Get, Set, Subscribe or Unsubscribe request
 *
 * @param encoder CBOR encoder
 * @param id The peer's item ID
 *
 * @return SUCCESS when the ID was encoded
 * @return -ENOMEM when the encoder lacks room
 */
int stow_client_serde_encode_id(zcbor_state_t* encoder, uint32_t id);

/**
 * @brief Encode a Describe request for named items
 *
 * @param encoder CBOR encoder
 * @param tag Request tag
 * @param start_id ID to describe from
 * @param items Items whose names are the filter
 * @param count Number of items
 *
 * @return SUCCESS when the request was encoded
 * @return -ENOMEM when the encoder lacks room
 */
int stow_client_serde_encode_describe(
    zcbor_state_t* encoder, uint8_t tag, uint32_t start_id, const struct stow_client_item* items, size_t count);

/**
 * @brief Count how many names fit a Describe request
 *
 * @param budget Bytes available for the request
 * @param start_id ID to describe from
 * @param items Items whose names are the filter
 * @param count Number of items
 *
 * @return The number of leading items whose names fit, which may be 0
 */
size_t stow_client_serde_describe_fit(
    size_t budget, uint32_t start_id, const struct stow_client_item* items, size_t count);

/**
 * @brief Decode the body of a Hello Response
 *
 * @param decoder Decoder after the tag
 * @param[out] hello_out Populated with the server's limit and the session's roles
 *
 * @return SUCCESS when the response was decoded
 * @return -EPROTONOSUPPORT when the protocol version is not supported
 * @return -EBADMSG when it is malformed
 */
int stow_client_serde_decode_hello(zcbor_state_t* decoder, struct stow_client_serde_hello* hello_out);

/**
 * @brief Decode the body of a Describe Response
 *
 * @param decoder Decoder after the tag
 * @param[out] next_id_out ID to continue from, or @ref STOW_SERDE_ABSENT when the description is complete
 * @param[out] chunk_out The chunk, a CBOR sequence of item descriptions
 *
 * @return SUCCESS when the response was decoded
 * @return -EBADMSG when it is malformed
 */
int stow_client_serde_decode_describe(zcbor_state_t* decoder, uint32_t* next_id_out, struct zcbor_string* chunk_out);

/**
 * @brief Decode the next item description of a chunk
 *
 * @param decoder Decoder over the chunk
 * @param[out] description_out Populated with the parts the client checks
 *
 * @return SUCCESS when a description was decoded
 * @return -EBADMSG when it is malformed
 */
int stow_client_serde_decode_description(zcbor_state_t* decoder, struct stow_client_serde_description* description_out);

/**
 * @brief Check a peer's item description against the expected item
 *
 * @details The expected constraints are encoded with the item's own codec and compared byte for byte.
 *
 * @param item The expected item
 * @param description The peer's description of an item with the same name
 *
 * @return SUCCESS when the type and constraints match
 * @return -EINVAL when they differ
 * @return -ENOMEM when no memory is free to encode the expected constraints
 */
int stow_client_serde_check(
    const struct stow_client_item* item, const struct stow_client_serde_description* description);

/**
 * @brief Decode the body of an Error response as the errno the client reports
 *
 * @param decoder Decoder after the tag
 * @param[out] index_out Entry that failed as it was applied, or @ref STOW_SERDE_ABSENT
 *
 * @return The errno for the server's error code, or -ENOMSG when the Error itself is malformed
 */
int stow_client_serde_decode_error(zcbor_state_t* decoder, uint32_t* index_out);

/**
 * @}
 */
