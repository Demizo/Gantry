/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Provides safe alternatives to common string functions
 *
 * @author Demizo (demizodemazo@gmail.com)
 *
 *
 */

#pragma once

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
