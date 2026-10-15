/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Serialization and deserialization of Stow protocol messages
 *
 * @details A stateless CBOR encoder and decoder for the Stow protocol.
 *
 * @author Demizo (demizodemazo@gmail.com)
 *
 */

#pragma once

#include <gantry/stow/stow_wire.h>
#include <gantry/stow/types/stow_types.h>
#include <generated_stow_items.h>
#include <stddef.h>
#include <stdint.h>
#include <zcbor_common.h>
#include <zephyr/sys/atomic.h>

/**
 * @addtogroup stow_protocol
 * @{
 */

//**********************************************************
//* Definitions
//**********************************************************

//**********************************************************
//* Typedefs, Enums, and Structs
//**********************************************************

/**
 * @brief Contents of a Hello Response
 */
struct stow_serde_hello
{
    uint32_t max_message;  /**< Largest message the server can send, in bytes */
    const char* stow_hash; /**< Current StowHash value */
    stow_role_t roles;     /**< Roles the session holds */
};

/**
 * @brief A decoded Stow protocol request
 *
 * @details Entries are not kept; decode them again with @ref stow_serde_decode_entry.
 */
struct stow_serde_request
{
    enum stow_message_code message_code; /**< Message code */
    uint8_t tag;                         /**< Request tag, or @ref STOW_TAG_NONE when it could not be decoded */
    enum stow_item_id item_id;           /**< Start ID of a Describe request */

    const uint8_t* payload; /**< The encoded request, kept to decode its entries again */
    size_t payload_len;     /**< Length of @ref payload */
    uint16_t entry_count;   /**< Number of entries in a Get, Set, Subscribe or Unsubscribe request */

    bool has_filter;                      /**< Whether a Describe request named the items to describe */
    ATOMIC_DEFINE(filter, STOW_ID_COUNT); /**< Items named by a Describe request; unknown names are ignored */

    uint32_t error_item; /**< Item that failed to decode, or @ref STOW_SERDE_ABSENT */
};

//**********************************************************
//* Functions
//**********************************************************

/**
 * @brief Decode and check a complete Stow protocol request
 *
 * @details Every entry is checked without being applied; unknown items are reported when the entries are applied.
 *
 * @param[in] decoder CBOR decoder instance positioned at the start of the request array
 * @param[out] request_out Populated with the decoded request
 *
 * @return SUCCESS when the request was decoded
 * @return -EBADMSG when the request is malformed
 * @return -ENOTSUP when the message code is unknown
 * @return -ENOENT when a Describe start ID is not valid
 * @return -ENOMEM when memory cannot be allocated to check a value
 */
int stow_serde_decode_request(zcbor_state_t* decoder, struct stow_serde_request* request_out);

/**
 * @brief Start decoding the entries of a request again
 *
 * @param request A request that @ref stow_serde_decode_request accepted
 * @param[out] decoder Decoder positioned at the request's first entry
 * @param states Number of states in @p decoder
 */
void stow_serde_entries_start(const struct stow_serde_request* request, zcbor_state_t* decoder, size_t states);

/**
 * @brief Decode the item ID of the next entry
 *
 * @details A Set entry's value follows the ID; decode it with @ref stow_decode.
 *
 * @param decoder Decoder from @ref stow_serde_entries_start
 * @param[out] id_out Item the entry names
 *
 * @return SUCCESS when the entry names a known item
 * @return -ENOENT when it names an unknown item
 */
int stow_serde_decode_entry(zcbor_state_t* decoder, uint32_t* id_out);

/**
 * @brief Encode a Hello Response message
 *
 * @param encoder CBOR encoder instance
 * @param tag Tag of the request being answered
 * @param hello Limits and session details to report
 *
 * @return SUCCESS when the response was encoded
 * @return -ENOMEM when the encoder lacks room
 */
int stow_serde_encode_hello_response(zcbor_state_t* encoder, uint8_t tag, const struct stow_serde_hello* hello);

/**
 * @brief Encode a Describe Response message
 *
 * @param encoder CBOR encoder instance
 * @param tag Tag of the request being answered
 * @param next_item_id The item ID for the client's next Describe request, or @ref STOW_SERDE_ABSENT when done
 * @param chunk Buffer holding the CBOR describe chunk, which may lie later in the encoder's own buffer
 * @param chunk_len Length of the chunk in bytes
 *
 * @return SUCCESS when the response was encoded
 * @return -ENOMEM when the encoder lacks room
 */
int stow_serde_encode_describe_response(
    zcbor_state_t* encoder, uint8_t tag, uint32_t next_item_id, const uint8_t* chunk, size_t chunk_len);

/**
 * @brief Encode the start of a Get Response message
 *
 * @details Follow it with @p count calls to @ref stow_serde_encode_get_entry.
 *
 * @param encoder CBOR encoder instance
 * @param tag Tag of the request being answered
 * @param count Number of id/value pairs that follow
 *
 * @return SUCCESS when the start was encoded
 * @return -ENOMEM when the encoder lacks room
 */
int stow_serde_encode_get_start(zcbor_state_t* encoder, uint8_t tag, uint16_t count);

/**
 * @brief Encode one id/value pair of a Get Response message
 *
 * @param encoder CBOR encoder instance
 * @param id Item ID
 * @param value The item's value
 *
 * @return SUCCESS when the pair was encoded
 * @return -ENOMEM when the encoder lacks room
 */
int stow_serde_encode_get_entry(zcbor_state_t* encoder, enum stow_item_id id, data_value_t value);

/**
 * @brief Encode an Update message
 *
 * @param encoder CBOR encoder instance
 * @param id Item ID of the updated value
 * @param value The item's updated value
 *
 * @return SUCCESS when the message was encoded
 * @return -ENOMEM when the encoder lacks room
 */
int stow_serde_encode_update(zcbor_state_t* encoder, enum stow_item_id id, data_value_t value);

/**
 * @brief Encode an OK response
 *
 * @param encoder CBOR encoder instance
 * @param tag Tag of the request being answered
 *
 * @return SUCCESS when the response was encoded
 * @return -ENOMEM when the encoder lacks room
 */
int stow_serde_encode_ok(zcbor_state_t* encoder, uint8_t tag);

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
int stow_serde_encode_error(zcbor_state_t* encoder, const struct stow_serde_error* error);

/**
 * @}
 */
