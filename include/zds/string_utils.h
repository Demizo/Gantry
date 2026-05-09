/**
 * @file string_utils.h
 *
 * @brief Provides safe alternatives to common string functions
 *
 * @author Demizo (demizodemazo@gmail.com)
 * @version 0.1
 * @date 2026-03-01
 *
 * @copyright Copyright (c) 2026
 *
 */

#ifndef STRING_UTILS_H
#define STRING_UTILS_H

#include <stddef.h>

/**
 * @brief Copy a NUL-terminated string into a sized buffer.
 *
 * @param dest   Where to copy the string to.
 * @param src    Where to copy the string from.
 * @param count  Size of destination buffer.
 *
 * @return Number of characters copied (excluding NUL)
 * @return -E2BIG if the destination buffer was too small.
 */
size_t strscpy(char* dest, const char* src, size_t count);

#endif  // STRING_UTILS_H