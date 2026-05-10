/**
 * @file
 * @brief Implements the datastore buffer type
 *
 * @author Demizo (demizodemazo@gmail.com)
 * @date 2026-03-01
 *
 *
 */

#ifndef DATASTORE_TYPE_BUFFER_H
#define DATASTORE_TYPE_BUFFER_H

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
