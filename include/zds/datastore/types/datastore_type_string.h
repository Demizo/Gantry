/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Implements the datastore string type
 *
 * @author Demizo (demizodemazo@gmail.com)
 *
 *
 */

#ifndef DATASTORE_TYPE_STRING_H
#define DATASTORE_TYPE_STRING_H

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
 * @brief Datastore interface for string items
 *
 * @details Operations expect to receive the address of a char pointer
 */
extern const struct datastore_item_interface datastore_string_interface;

//**********************************************************
//* Functions
//**********************************************************

/**
 * @}
 */

#endif  // DATASTORE_TYPE_STRING_H
