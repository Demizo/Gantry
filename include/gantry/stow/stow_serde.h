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

#ifndef STOW_SERDE_H
#define STOW_SERDE_H

#include <gantry/stow/types/stow_types.h>
#include <generated_stow_items.h>
#include <stddef.h>
#include <stdint.h>
#include <zcbor_common.h>

/**
 * @addtogroup stow_protocol
 * @{
 */

//**********************************************************
//* Definitions
//**********************************************************

/**
 * @brief Stow protocol version reported in Version Response
 */
#define STOW_PROTOCOL_VERSION 1

//**********************************************************
//* Typedefs, Enums, and Structs
//**********************************************************

/**
 * @brief Stow protocol message codes
 */
enum stow_message_code
{
    STOW_MSG_VERSION = 0,
    STOW_MSG_VERSION_RESPONSE = 1,
    STOW_MSG_DESCRIBE = 2,
    STOW_MSG_DESCRIBE_RESPONSE = 3,
    STOW_MSG_GET = 4,
    STOW_MSG_GET_RESPONSE = 5,
    STOW_MSG_SET = 6,
    STOW_MSG_MULTI_GET = 7,
    STOW_MSG_MULTI_GET_RESPONSE = 8,
    STOW_MSG_MULTI_SET = 9,
    STOW_MSG_SUBSCRIBE = 10,
    STOW_MSG_UNSUBSCRIBE = 11,
    STOW_MSG_UPDATE = 12,
    STOW_MSG_OK = 13,
    STOW_MSG_ERROR = 14,
    STOW_MSG_COUNT,
};

/**
 * @brief Stow protocol error codes used in Error responses
 */
enum stow_error_code
{
    STOW_ERR_MALFORMED_MSG = 1,     /**< Malformed message */
    STOW_ERR_UNKNOWN_MSG = 2,       /**< Unrecognized message code */
    STOW_ERR_INVALID_ITEM = 3,      /**< Invalid item ID */
    STOW_ERR_OUT_OF_MEMORY = 4,     /**< Out of memory */
    STOW_ERR_PERMISSION_DENIED = 5, /**< Role insufficient for this item */
    STOW_ERR_UNKNOWN = 6,           /**< Unknown error */
};

/**
 * @brief A fully decoded Stow protocol request
 *
 * @details For SET, @ref has_value is true and the caller owns @ref value.
 * For MULTI_SET, owned values are tracked via @ref multi_has_value.
 * All owned values must be released via @ref stow_serde_release_request.
 */
struct stow_serde_request
{
    enum stow_message_code message_code; /**< Message code */
    enum stow_item_id item_id;           /**< Item ID (GET/SET/SUB/UNSUB) or start ID (DESCRIBE) */
    bool has_value;                      /**< Whether @ref value is populated (SET) */
    data_value_t value;                  /**< Decoded value (SET only) */

    uint8_t multi_count;                                               /**< Number of items (MULTI_GET/MULTI_SET) */
    enum stow_item_id multi_ids[CONFIG_STOW_PROTOCOL_MULTI_MAX_ITEMS]; /**< Item IDs for multi operations */
    data_value_t multi_values[CONFIG_STOW_PROTOCOL_MULTI_MAX_ITEMS];   /**< Decoded values (MULTI_SET) */
    bool multi_has_value[CONFIG_STOW_PROTOCOL_MULTI_MAX_ITEMS];        /**< Ownership flags for multi values */
};

//**********************************************************
//* Functions
//**********************************************************

/**
 * @brief Decode a complete Stow protocol request message
 *
 * @details Attempts to decode the CBOR array as a command.
 * For SET, the value is decoded via @ref stow_decode.
 *
 * @note Regardless of return value, callers should invoke
 * @ref stow_serde_release_request on the request to release any partially
 * decoded value.
 *
 * @param[in] decoder CBOR decoder instance positioned at the start of the request array
 * @param[out] request_out Populated with the decoded request
 *
 * @return SUCCESS when the request was fully decoded
 * @return -EBADMSG when the CBOR is malformed
 * @return -ENOTSUP when the command code is unknown
 * @return -EINVAL when the item ID is not valid
 * @return -ENOMEM when memory cannot be allocated for the decoded value
 */
int stow_serde_decode_request(zcbor_state_t* decoder, struct stow_serde_request* request_out);

/**
 * @brief Release any owned value within a decoded request
 *
 * @details Safe to call on a request that was never populated or that failed
 * to decode.
 *
 * @param[in,out] request The request to release
 */
void stow_serde_release_request(struct stow_serde_request* request);

/**
 * @brief Encode a Version Response message
 *
 * @param encoder CBOR encoder instance
 *
 * @return SUCCESS when the response was encoded
 * @return -ENOMEM when the encoder lacks room
 */
int stow_serde_encode_version_response(zcbor_state_t* encoder);

/**
 * @brief Encode a Describe Response message
 *
 * @param encoder      CBOR encoder instance
 * @param next_item_id The item ID the client should use in its next Describe request
 * @param has_more     Whether more items remain after this chunk
 * @param chunk        Buffer holding the CBOR describe chunk
 * @param chunk_len    Length of the chunk in bytes
 *
 * @return SUCCESS when the response was encoded
 * @return -ENOMEM when the encoder lacks room
 */
int stow_serde_encode_describe_response(
    zcbor_state_t* encoder, uint32_t next_item_id, bool has_more, const uint8_t* chunk, size_t chunk_len);

/**
 * @brief Encode a Get Response message
 *
 * @param encoder CBOR encoder instance
 * @param id The item's ID
 * @param value The item's value
 *
 * @return SUCCESS when the response was encoded
 * @return -ENOMEM when the encoder lacks room
 */
int stow_serde_encode_get_response(zcbor_state_t* encoder, enum stow_item_id id, data_value_t value);

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
 * @brief Encode a Multi-Get Response message
 *
 * @param encoder CBOR encoder instance
 * @param ids     Array of item IDs, length @p count
 * @param values  Array of item values, length @p count
 * @param count   Number of id/value pairs
 *
 * @return SUCCESS when the response was encoded
 * @return -ENOMEM when the encoder lacks room
 */
int stow_serde_encode_multi_get_response(
    zcbor_state_t* encoder, const enum stow_item_id* ids, const data_value_t* values, uint8_t count);

/**
 * @brief Encode an OK response
 *
 * @param encoder CBOR encoder instance
 *
 * @return SUCCESS when the response was encoded
 * @return -ENOMEM when the encoder lacks room
 */
int stow_serde_encode_ok(zcbor_state_t* encoder);

/**
 * @brief Encode an Error response
 *
 * @param encoder CBOR encoder instance
 * @param code    Protocol error code describing the failure
 *
 * @return SUCCESS when the response was encoded
 * @return -ENOMEM when the encoder lacks room
 */
int stow_serde_encode_error(zcbor_state_t* encoder, enum stow_error_code code);

/**
 * @}
 */

#endif  // STOW_SERDE_H
