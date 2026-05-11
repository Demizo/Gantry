/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Implements the stow buffer type
 *
 * @author Demizo (demizodemazo@gmail.com)
 *
 *
 */

#ifndef STOW_TYPE_BUFFER_H
#define STOW_TYPE_BUFFER_H

#include <stddef.h>
#include <gantry/stow/types/stow_types.h>

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

/**
 * @brief Stow interface for buffer items
 *
 * @details Operations expect to receive the address of a @ref buffer_t pointer
 */
extern const struct stow_item_interface stow_buffer_interface;

//**********************************************************
//* Functions
//**********************************************************

/**
 * @}
 */

#endif  // STOW_TYPE_BUFFER_H
