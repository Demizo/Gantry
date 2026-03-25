/**
 * @file datastore_type_buffer.h
 *
 * @brief Implements the datastore buffer type
 *
 * @author Demizo (demizodemazo@gmail.com)
 * @version 0.1
 * @date 2026-03-01
 *
 * @copyright Copyright (c) 2026
 *
 */

#ifndef DATASTORE_TYPE_BUFFER_H
#define DATASTORE_TYPE_BUFFER_H

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
 * @brief Datastore interface for buffer items
 *
 * @details Operations expect to receive the address of a @ref buffer_t pointer
 */
extern const struct datastore_item_interface datastore_buffer_interface;

//**********************************************************
//* Functions
//**********************************************************

/**
 * @}
 */

#endif  // DATASTORE_TYPE_BUFFER_H
