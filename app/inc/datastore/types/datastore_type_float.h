/**
 * @file datastore_type_float.h
 *
 * @brief Implements the datastore float type
 *
 * @author Demizo (demizodemazo@gmail.com)
 * @version 0.1
 * @date 2026-03-01
 *
 * @copyright Copyright (c) 2026
 *
 */

#ifndef DATASTORE_TYPE_FLOAT_H
#define DATASTORE_TYPE_FLOAT_H

#include <stddef.h>

#include "datastore_types.h"

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
