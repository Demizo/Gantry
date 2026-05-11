/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Non-volatile storage backend for the stow
 *
 * @author Demizo (demizodemazo@gmail.com)
 *
 *
 */

#ifndef STOW_STORAGE_H
#define STOW_STORAGE_H

#include <stddef.h>
#include <zds/stow/types/stow_types.h>
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

//**********************************************************
//* Functions
//**********************************************************

/**
 * @brief Load data items from storage
 *
 * @details This should be called during stow initialization
 *
 * @return SUCCESS when the stow has been loaded from storage, non-zero on error
 */
int stow_storage_load(void);

/**
 * @brief Save an item to persistent storage
 *
 * @details The value is only saved if it has changed.
 *
 * @param item Data item to save
 *
 * @return SUCCESS when the item is saved
 * @return -ENOMEM when memory is not available
 * @return result of settings_save_one on failure
 */
int stow_storage_save_item(const struct stow_item_const_metadata* item);

/**
 * @}
 */

#endif  // STOW_STORAGE_H
