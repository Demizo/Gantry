/**
 * @file
 * @brief Implements the datastore float type
 *
 * @author Demizo (demizodemazo@gmail.com)
 * @date 2026-03-01
 *
 *
 */

#ifndef DATASTORE_TYPE_FLOAT_H
#define DATASTORE_TYPE_FLOAT_H

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
 * @brief Datastore interface for float items
 *
 * @details Operations expect to receive the address of a float
 */
extern const struct datastore_item_interface datastore_float_interface;

//**********************************************************
//* Functions
//**********************************************************

/**
 * @}
 */

#endif  // DATASTORE_TYPE_FLOAT_H
