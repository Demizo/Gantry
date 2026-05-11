/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Implements the stow string type
 *
 * @author Demizo (demizodemazo@gmail.com)
 *
 *
 */

#ifndef STOW_TYPE_STRING_H
#define STOW_TYPE_STRING_H

#include <stddef.h>
#include <zds/stow/types/stow_types.h>

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
 * @brief Stow interface for string items
 *
 * @details Operations expect to receive the address of a char pointer
 */
extern const struct stow_item_interface stow_string_interface;

//**********************************************************
//* Functions
//**********************************************************

/**
 * @}
 */

#endif  // STOW_TYPE_STRING_H
