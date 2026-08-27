/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Defines the supported stow item types and the interface that they must implement
 *
 * @author Demizo (demizodemazo@gmail.com)
 *
 *
 */

#pragma once

#include <gantry/buffer.h>
#include <stddef.h>
#include <stdint.h>
#include <zcbor_decode.h>
#include <zcbor_encode.h>
#include <zephyr/kernel.h>

/**
 * @addtogroup stow
 * @{
 */

//**********************************************************
//* Definitions
//**********************************************************

//**********************************************************
//* Typedefs, Enums, and Structs
//**********************************************************

/**
 * @brief Supported item data types
 */
enum stow_item_type
{
    STOW_ITEM_TYPE_ENUM,
    STOW_ITEM_TYPE_INT,
    STOW_ITEM_TYPE_FLOAT,
    STOW_ITEM_TYPE_STRING,
    STOW_ITEM_TYPE_BYTE_ARRAY,
    STOW_ITEM_TYPE_BUFFER,
    STOW_ITEM_TYPE_STRUCT,
    STOW_ITEM_TYPE_COUNT
};

/**
 * @brief String representation of each item type
 */
extern const char* const item_type_strings[];

/**
 * @brief Union of raw values for each data type
 */
typedef union
{
    int int_value;          /**< Value for ints */
    float float_value;      /**< Value for floats */
    char* string_value;     /**< Value for strings */
    buffer_t* buffer_value; /**< Value for buffers */
    void* raw_value;        /**< Raw pointer value for structs */
} raw_data_value_t;

/**
 * @brief Tagged union representing a data item value
 */
typedef struct
{
    enum stow_item_type type; /**< Data value type */
    raw_data_value_t data;    /**< Data value */
} data_value_t;

/**
 * @brief Storage types for stow items
 */
enum stow_storage_type
{
    STOW_STORAGE_EPHEMERAL,  /**< Items are reset to their default value upon reboot */
    STOW_STORAGE_PERSISTENT, /**< Items are stored in non-volatile storage. Their values persist across reboots. */
    STOW_STORAGE_TOFU,       /**< Item can be written while at its default value. It cannot be changed again. */
    STOW_STORAGE_COUNT
};

/**
 * @brief Role bitmask type for access control.
 *
 * Each bit position represents one role from the app's Stow specification. Permission checks are a bitwise AND
 * between a caller's role bitmask and an item's permission mask.
 *
 * @note @ref STOW_ROLE_INTERNAL marks and item as internal-only since no external role bit mask
 * could pass the check.
 */
typedef uint16_t stow_role_t;

#define STOW_ROLE_INTERNAL ((stow_role_t)0x0000U) /**< Internal-only; no external client access */
#define STOW_ROLE_ANY ((stow_role_t)0xFFFFU)      /**< Any external client role has access */

/**
 * @brief Permission bitmasks required to read or write an associated data item
 */
struct stow_permissions
{
    stow_role_t read_permissions;  /**< Role bitmask required to read the item value */
    stow_role_t write_permissions; /**< Role bitmask required to write the item value */
};

/**
 * @brief Value and name pair for an enum value
 */
struct data_enum_value
{
    int value;  /**< Numeric enum value */
    char* name; /**< Name of the enum value */
};

/**
 * @brief Constraints for enum items
 */
struct stow_enum_constraints
{
    const uint16_t value_count;           /**< Number of possible enum values */
    const struct data_enum_value* values; /**< Possible enum values */
};

/**
 * @brief Constraints for int items
 */
struct stow_int_constraints
{
    int32_t min; /**< Minimum value */
    int32_t max; /**< Maximum value */
};

/**
 * @brief Constraints for float items
 */
struct stow_float_constraints
{
    float min; /**< Minimum value */
    float max; /**< Maximum value */
};

/**
 * @brief Constraints for variable length buffer items
 */
struct stow_buffer_constraints
{
    uint16_t min_len; /**< Minimum length */
    uint16_t max_len; /**< Maximum length */
};

/**
 * @brief Constraints for item value
 */
union stow_constraints
{
    struct stow_enum_constraints enum_constraints;   /**< Constraints for enum items */
    struct stow_int_constraints int_constraints;     /**< Constraints for int items */
    struct stow_float_constraints float_constraints; /**< Constraints for float items */
    struct stow_buffer_constraints
        buffer_constraints; /**< Constraints for variable length buffer types (string, bytes, buffers) */
};

// Forward declare constant stow metadata
struct stow_item_const_metadata;

/**
 * @brief Optional application-provided interface overrides for an item.
 *
 * @details Each field may be NULL indicating that the function has no custom implementation.
 */
struct stow_item_custom_interface
{
    /**
     * @brief Custom get function to override @ref stow_item_interface.get
     *
     * @details Invoked by @ref stow_get (and by subscriber notifications)
     */
    int (*get)(const struct stow_item_const_metadata* item, data_value_t* out_value);
    /**
     * @brief Custom set function to override @ref stow_item_interface.set
     *
     * @details Invoked by @ref stow_set after the validation checks
     */
    int (*set)(const struct stow_item_const_metadata* item, data_value_t value);
    /**
     * @brief Custom validator called after default validation
     *
     * @note This should be avoided whenever possible since clients will not know custom validation rules without prior
     * knowledge of the system.
     *
     * @details Invoked by @ref stow_set after the regular type-level validation succeeds, as an additional gate.
     * Returns True to accept, False to reject.
     *
     */
    bool (*validate)(const struct stow_item_const_metadata* item, data_value_t value);
};

/**
 * @brief Common interface for each stow item, implemented for each item type
 */
struct stow_item_interface
{
    bool (*validate)(
        const union stow_constraints* constraints,
        data_value_t value); /**< Function to determine if a given value is valid based on the provided constraints */
    bool (*is_equal)(data_value_t a, data_value_t b); /**< Function to check if two values of the same type are equal */
    void (*set)(void* dest, data_value_t value);    /**< Function to copy a value's data to the provided destination */
    int (*get)(void* src, data_value_t* out_value); /**< Function to get an item value from the provided source */
    void (*release)(data_value_t* value); /**< Function to release an item value. Note: the inner value may be NULL in
                                             which case release should do nothing. */
    int (*decode)(zcbor_state_t* decoder, data_value_t* out_value); /**< Function to decode an item value from CBOR */
    int (*encode)(zcbor_state_t* encoder, data_value_t value); /**< Function to encode a data item value into CBOR */
    int (*encode_constraints)(
        zcbor_state_t* encoder,
        const union stow_constraints* constraints); /**< Function to encode the type-specific constraints */
};

/**
 * @brief Constant metadata for stow items
 */
struct stow_item_const_metadata
{
    uint32_t id;                   /**< A stow item ID, see stow_item_id */
    const char* name;              /**< Name of the item. This must be unique, including across firmware versions */
    const char* const* categories; /**< Pointer to array of category names */
    uint8_t category_count;        /**< Number of categories */
    enum stow_storage_type storage_type;         /**< Storage type of the data item */
    struct stow_permissions permissions;         /**< Read/write permissions for the data item */
    enum stow_item_type type;                    /**< Type of the data item */
    const struct stow_item_interface* interface; /**< Common interface for data items */
    const struct stow_item_custom_interface*
        custom_interface;                 /**< Optional custom interface for set, get, and validate */
    void* value_ptr;                      /**< Pointer to the item's value */
    const raw_data_value_t default_value; /**< The item's default value */
    union stow_constraints constraints;   /**< The item's value constraints */
};

//**********************************************************
//* Functions
//**********************************************************

/**
 * @}
 */
