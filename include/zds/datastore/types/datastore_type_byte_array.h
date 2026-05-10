/**
 * @file
 * @brief Implements the datastore byte array type
 *
 * @author Demizo (demizodemazo@gmail.com)
 * @date 2026-03-01
 *
 *
 */

#ifndef DATASTORE_TYPE_BYTE_ARRAY_H
#define DATASTORE_TYPE_BYTE_ARRAY_H

#include <stddef.h>
#include <zds/datastore/types/datastore_types.h>

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
 * @brief Datastore interface for byte array items
 *
 * @details Operations expect to receive the address of a @ref buffer_t pointer
 */
extern const struct datastore_item_interface datastore_byte_array_interface;

//**********************************************************
//* Functions
//**********************************************************

/**
 * @}
 */

#endif  // DATASTORE_TYPE_BYTE_ARRAY_H
