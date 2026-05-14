/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Implements the stow enum type
 *
 * @author Demizo (demizodemazo@gmail.com)
 *
 *
 */

#ifndef STOW_TYPE_ENUM_H
#define STOW_TYPE_ENUM_H

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
 * @brief Stow interface for enum items
 *
 * @details Operations expect to receive the address of an int
 */
extern const struct stow_item_interface stow_enum_interface;

//**********************************************************
//* Functions
//**********************************************************

/**
 * @brief Get the enum value's name from its numeric value
 *
 * @param[in] constraints the enum's constraints
 * @param[in] value the numeric enum value
 * @param[out] out_name the name of the enum value, only populated on success
 * @return int SUCCESS if the value existed
 * @return -EINVAL when the provided value was invalid
 */
int enum_get_name_from_value(const union stow_constraints* constraints, int value, char** out_name);

/**
 * @brief Get the enum value's numeric value from its name
 *
 * @param[in] constraints the enum's constraints
 * @param[in] name the name of the enum value
 * @param[out] out_value numeric value of the enum value, only populated on success
 * @return int SUCCESS if the name was valid
 * @return -EINVAL when the provided name was invalid
 */
int enum_get_value_from_name(const union stow_constraints* constraints, char* name, int* out_value);

/**
 * @}
 */

#endif  // STOW_TYPE_ENUM_H
