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
    DATASTORE_ITEM_TYPE_BYTE_BUFFER,
    DATASTORE_ITEM_TYPE_DYNAMIC_BUFFER,
    DATASTORE_ITEM_TYPE_COUNT
};

/**
 * @brief Storage types for datastore items
 */
enum datastore_storage_type
{
    DATASTORE_STORAGE_CONSTANT,   /**< Value is defined at compile time and cannot be changed */
    DATASTORE_STORAGE_EPHEMERAL,  /**< Items are reset to their default value upon reboot */
    DATASTORE_STORAGE_PERSISTENT, /**< Items are stored in non-volatile storage. Their values persist across reboots. */
    DATASTORE_STORAGE_TOFU,       /**< Item can be written while at its default value. It cannot be changed again. */
    DATASTORE_STORAGE_COUNT
};

/**
 * @brief Permission levels required to read or write an associated data item
 */
struct datastore_item_permissions
{
    uint8_t read_permissions;  /**< Permission level required to read the item value */
    uint8_t write_permissions; /**< Permission level required to write the item value */
};

/**
 * @brief Metadata for enum items
 */
struct datastore_enum_info
{
    uint8_t default_value; /**< Default value */
    uint8_t min;           /**< Minimum value */
    uint8_t max;           /**< Maximum value */
};

/**
 * @brief Metadata for int items
 */
struct datastore_int_info
{
    int32_t default_value; /**< Default value */
    int32_t min;           /**< Minimum value */
    int32_t max;           /**< Maximum value */
};

/**
 * @brief Metadata for float items
 */
struct datastore_float_info
{
    float default_value; /**< Default value */
    float min;           /**< Minimum value */
    float max;           /**< Maximum value */
};

/**
 * @brief Metadata for string items
 */
struct datastore_string_info
{
    const char* default_value; /**< Default value */
    uint16_t max_len;          /**< Maximum length */
};

/**
 * @brief Metadata for byte array items
 */
struct datastore_byte_array_info
{
    const uint8_t* default_value; /**< Default value */
    uint16_t size;                /**< Size in bytes */
};

/**
 * @brief Type specific item info
 */
union datastore_type_info
{
    struct datastore_enum_info enum_info;             /**< Info for enum items */
    struct datastore_int_info int_info;               /**< Info for int items */
    struct datastore_float_info float_info;           /**< Info for float items */
    struct datastore_string_info string_info;         /**< Info for string items */
    struct datastore_byte_array_info byte_array_info; /**< Info for byte array items */
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
        const void* value); /**< Function to determine if a given value is valid */
    void (*set)(
        const struct datastore_item_const_metadata* item, const void* value); /**< Function to set an item value */
    int (*get)(
        const struct datastore_item_const_metadata* item, void** out_value); /**< Function to get an item value */
    int (*release)(
        const struct datastore_item_const_metadata* item, void** value); /**< Function to release an item value */
    // TODO: Add encode and decode functions:
    // - Decode takes (metadata, decoder, void** out_data)
    // - Encode takes (metadata, encoder, void* data)
};

/**
 * @brief Constant metadata for datastore items
 */
struct datastore_item_const_metadata
{
    uint32_t id;      /**< A datastore item ID, see @ref enum datastore_item_id */
    const char* name; /**< Name of the item. This must be unique, including across firmware versions */
    enum datastore_storage_type storage_type;         /**< Storage type of the data item */
    struct datastore_item_permissions permissions;    /**< Read/write permissions for the data item */
    enum datastore_item_type type;                    /**< Type of the data item */
    const struct datastore_item_interface* interface; /**< Common interface for data items */
    void* value_ptr;                                  /**< Pointer to the item's value */
    union datastore_type_info type_info;              /**< Type specific information */
};

//**********************************************************
//* Functions
//**********************************************************

/**
 * @}
 */

#endif  // DATASTORE_TYPES_H
