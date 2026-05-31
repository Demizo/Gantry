/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Implements the stow byte array type
 *
 * @author Demizo (demizodemazo@gmail.com)
 *
 *
 */

#pragma once

#include <gantry/stow/types/stow_types.h>
#include <stddef.h>

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
 * @brief Stow interface for byte array items
 *
 * @details Operations expect to receive the address of a @ref buffer_t pointer
 */
extern const struct stow_item_interface stow_byte_array_interface;

//**********************************************************
//* Functions
//**********************************************************

/**
 * @}
 */
