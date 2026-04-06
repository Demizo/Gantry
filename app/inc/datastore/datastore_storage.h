/**
 * @file datastore_storage.h
 *
 * @brief Non-volatile storage backend for the datastore
 *
 * @author Demizo (demizodemazo@gmail.com)
 * @version 0.1
 * @date 2026-03-01
 *
 * @copyright Copyright (c) 2026
 *
 */

#ifndef DATASTORE_STORAGE_H
#define DATASTORE_STORAGE_H

#include <stddef.h>
#include <zephyr/kernel.h>

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

//**********************************************************
//* Functions
//**********************************************************

/**
 * @brief Load data items from storage
 *
 * @details This should be called during datastore initialization
 *
 * @return SUCCESS when the datastore has been loaded from storage, non-zero on error
 */
int datastore_storage_load(void);

/**
 * @brief Save an item to persistent storage
 *
 * @details The value is only saved if it has changed.
 *
 * @param item Data item to save
 *
 * @return SUCCESS when the item is saved
 * @return -ENOMEM when memory is not available
 * @return result of @ref settings_save_one on failure
 */
int datastore_storage_save_item(const struct datastore_item_const_metadata* item);

/**
 * @}
 */

#endif  // DATASTORE_STORAGE_H
