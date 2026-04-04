/**
 * @file datastore_types.h
 *
 * @brief Defines the supported datastore item types and the interface that they must implement
 *
 * @author Demizo (demizodemazo@gmail.com)
 * @version 0.1
 * @date 2026-03-01
 *
 * @copyright Copyright (c) 2026
 *
 */

#ifndef DATASTORE_TYPES_H
#define DATASTORE_TYPES_H

#include <stddef.h>
#include <stdint.h>
#include <zephyr/kernel.h>

#include "buffer.h"

/**
 * @addtogroup datastore
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
enum datastore_item_type
{
    DATASTORE_ITEM_TYPE_ENUM,
    DATASTORE_ITEM_TYPE_INT,
    DATASTORE_ITEM_TYPE_FLOAT,
    DATASTORE_ITEM_TYPE_STRING,
    DATASTORE_ITEM_TYPE_BYTE_ARRAY,
    DATASTORE_ITEM_TYPE_BUFFER,
    DATASTORE_ITEM_TYPE_COUNT
};

/**
 * @brief Union of raw values for each data type
 */
typedef union
{
    int int_value;
    float float_value;
    char* string_value;
    buffer_t* buffer_value;
} raw_data_value_t;

/**
 * @brief Tagged union representing a data item value
 */
typedef struct
{
    enum datastore_item_type type;
    raw_data_value_t data;
} data_value_t;

/**
 * @brief Storage types for datastore items
 */
enum datastore_storage_type
{
    DATASTORE_STORAGE_EPHEMERAL,  /**< Items are reset to their default value upon reboot */
    DATASTORE_STORAGE_PERSISTENT, /**< Items are stored in non-volatile storage. Their values persist across reboots. */
    DATASTORE_STORAGE_TOFU,       /**< Item can be written while at its default value. It cannot be changed again. */
    DATASTORE_STORAGE_COUNT
};

/**
 * @brief Authentication levels
 */
enum datastore_auth_level
{
    AUTH_NONE,     /**< Access does not require authentication */
    AUTH_SESSION,  /**< Access requires a authenticated session */
    AUTH_DEV,      /**< Only dev sessions have access */
    AUTH_INTERNAL, /**< Only internal modules have access */
    AUTH_NO_ACCESS /**< No access permitted */
};

/**
 * @brief Permission levels required to read or write an associated data item
 */
struct datastore_permissions
{
    enum datastore_auth_level read_permissions;  /**< Permission level required to read the item value */
    enum datastore_auth_level write_permissions; /**< Permission level required to write the item value */
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
struct datastore_enum_constraints
{
    const uint16_t value_count;           /**< Number of possible enum values */
    const struct data_enum_value* values; /**< Possible enum values */
};

/**
 * @brief Constraints for int items
 */
struct datastore_int_constraints
{
    int32_t min; /**< Minimum value */
    int32_t max; /**< Maximum value */
};

/**
 * @brief Constraints for float items
 */
struct datastore_float_constraints
{
    float min; /**< Minimum value */
    float max; /**< Maximum value */
};

/**
 * @brief Constraints for variable length buffer items
 */
struct datastore_buffer_constraints
{
    uint16_t min_len; /**< Minimum length */
    uint16_t max_len; /**< Maximum length */
};

/**
 * @brief Constraints for item value
 */
union datastore_constraints
{
    struct datastore_enum_constraints enum_constraints;   /**< Constraints for enum items */
    struct datastore_int_constraints int_constraints;     /**< Constraints for int items */
    struct datastore_float_constraints float_constraints; /**< Constraints for float items */
    struct datastore_buffer_constraints
        buffer_constraints; /**< Constraints for variable length buffer types (string, bytes, buffers) */
};

// Forward declare constant datastore metadata
struct datastore_item_const_metadata;

/**
 * @brief Common interface for each datastore item, implemented for each item type
 */
struct datastore_item_interface
{
    bool (*validate)(
        const struct datastore_item_const_metadata* item,
        data_value_t value); /**< Function to determine if a given value is valid */
    void (*set)(
        const struct datastore_item_const_metadata* item, data_value_t value); /**< Function to set an item value */
    int (*get)(
        const struct datastore_item_const_metadata* item,
        data_value_t* out_value);        /**< Function to get an item value */
    int (*release)(data_value_t* value); /**< Function to release an item value */
    bool (*is_default)(
        const struct datastore_item_const_metadata* item); /**< Function to check if an item is at its default value */
    // TODO: Add encode and decode functions:
    // - Decode takes (metadata, decoder, void** out_data)
    // - Encode takes (metadata, encoder, void* data)
};

/**
 * @brief Constant metadata for datastore items
 */
struct datastore_item_const_metadata
{
    uint32_t id;      /**< A datastore item ID, see @ref datastore_item_id */
    const char* name; /**< Name of the item. This must be unique, including across firmware versions */
    enum datastore_storage_type storage_type;         /**< Storage type of the data item */
    struct datastore_permissions permissions;         /**< Read/write permissions for the data item */
    enum datastore_item_type type;                    /**< Type of the data item */
    const struct datastore_item_interface* interface; /**< Common interface for data items */
    void* value_ptr;                                  /**< Pointer to the item's value */
    const raw_data_value_t default_value;             /**< The item's default value */
    union datastore_constraints constraints;          /**< The item's value constraints */
};

//**********************************************************
//* Functions
//**********************************************************

/**
 * @}
 */

#endif  // DATASTORE_TYPES_H
