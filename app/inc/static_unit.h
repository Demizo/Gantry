/**
 * @file static_unit.h
 *
 * @brief Provides a static definition that is compiled out for unit tests to expose select static functions to testing.
 *
 * @author Demizo (demizodemazo@gmail.com)
 * @version 0.1
 * @date 2026-03-01
 *
 * @copyright Copyright (c) 2026
 *
 */

#ifndef STATIC_UNIT_H
#define STATIC_UNIT_H

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

#endif  // STATIC_UNIT_H
