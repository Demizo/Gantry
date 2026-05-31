/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Buffer data type
 *
 * @details Buffers are arbitrary pointers with a length field. They are used throughout the application when
 * dynamically sized variables are needed.
 *
 * @author Demizo (demizodemazo@gmail.com)
 *
 *
 */

#pragma once

#include <stdint.h>

/**
 * @brief Buffer with length
 *
 * @details Used to store data of a variable length. The buffer data is aligned so that it can be interpreted as custom
 * types or structs.
 *
 */
typedef struct
{
    uint16_t len;                                          /**< Buffer length in bytes */
    uint8_t buf[] __attribute__((aligned(sizeof(void*)))); /**< Variable size buffer data */
} buffer_t;

/**
 * @brief Creates a buffer on the stack of a specified size
 */
#define STACK_BUFFER(name, size)                        \
    union                                               \
    {                                                   \
        buffer_t buffer;                                \
        uint8_t name##_data[sizeof(buffer_t) + (size)]; \
    } name##_storage = { 0 };                           \
    buffer_t*(name) = &name##_storage.buffer;           \
    (name)->len = (uint16_t)(size);
