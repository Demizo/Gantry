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

#include <stdbool.h>
#include <stdint.h>
#include <zcbor_common.h>
#include <zcbor_encode.h>

/**
 * @addtogroup stow_protocol
 * @{
 */

//**********************************************************
//* Definitions
//**********************************************************

/**
 * @brief Stow protocol version reported in Hello Response
 */
#define STOW_PROTOCOL_VERSION 1

/**
 * @brief Largest request tag a client may choose, so a tag always encodes as one byte
 */
#define STOW_TAG_MAX 23U

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
 * @brief Stow protocol message codes
 */
enum stow_message_code
{
    STOW_MSG_HELLO = 0,
    STOW_MSG_HELLO_RESPONSE = 1,
    STOW_MSG_DESCRIBE = 2,
    STOW_MSG_DESCRIBE_RESPONSE = 3,
    STOW_MSG_GET = 4,
    STOW_MSG_GET_RESPONSE = 5,
    STOW_MSG_SET = 6,
    STOW_MSG_SUBSCRIBE = 7,
    STOW_MSG_UNSUBSCRIBE = 8,
    STOW_MSG_UPDATE = 9,
    STOW_MSG_OK = 10,
    STOW_MSG_ERROR = 11,
    STOW_MSG_COUNT,
};

/**
 * @brief Keys of the map in a Hello Response
 *
 * @details Clients ignore keys they do not know, so the map can grow.
 */
enum stow_hello_key
{
    STOW_HELLO_MAX_MESSAGE = 0, /**< Largest message the server can send, in bytes */
    STOW_HELLO_STOW_HASH = 1,   /**< Current StowHash value */
    STOW_HELLO_ROLES = 2,       /**< Bitmask of the roles the session holds */
    STOW_HELLO_ROLE_NAMES = 3,  /**< Name of every role, indexed by its bit */
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
    STOW_ERR_INVALID_VALUE = 7,     /**< Value rejected by the item's constraints or validator */
    STOW_ERR_ALREADY_SET = 8,       /**< Trust-on-first-use item has already been set */
    STOW_ERR_TOO_LARGE = 9,         /**< Response does not fit the server's buffers */
};

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
 * @}
 */
