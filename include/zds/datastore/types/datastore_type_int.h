/**
 * @file datastore_type_int.h
 *
 * @brief Implements the datastore int type
 *
 * @author Demizo (demizodemazo@gmail.com)
 * @version 0.1
 * @date 2026-03-01
 *
 * @copyright Copyright (c) 2026
 *
 */

#ifndef DATASTORE_TYPE_INT_H
#define DATASTORE_TYPE_INT_H

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
 * @brief Datastore interface for int items
 *
 * @details Operations expect to receive the address of an integer
 */
extern const struct datastore_item_interface datastore_int_interface;

//**********************************************************
//* Functions
//**********************************************************

/**
 * @}
 */

#endif  // DATASTORE_TYPE_INT_H
