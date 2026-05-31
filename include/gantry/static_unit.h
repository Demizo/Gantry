/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Provides a static definition that is compiled out for unit tests to expose select static functions to testing.
 *
 * @author Demizo (demizodemazo@gmail.com)
 *
 *
 */

#pragma once

#include <autoconf.h>

//**********************************************************
//* Definitions
//**********************************************************

/**
 * @brief Static definition that is removed in unit testing builds.
 */
#ifdef CONFIG_UNIT_TESTING
#define STATIC_UNIT
#else
#define STATIC_UNIT static
#endif

//**********************************************************
//* Typedefs, Enums, and Structs
//**********************************************************

//**********************************************************
//* Functions
//**********************************************************
