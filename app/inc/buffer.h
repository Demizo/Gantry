/**
 * @file buffer.h
 *
 * @brief Buffer data type
 *
 * @details Buffers are arbitrary pointers with a length field. They are used throughout the application when
 * dynamically sized variables are needed.
 *
 * @author Demizo (demizodemazo@gmail.com)
 * @version 0.1
 * @date 2026-03-05
 *
 * @copyright Copyright (c) 2026
 *
 */

#ifndef BUFFER_H
#define BUFFER_H

#include <stdint.h>

/**
 * @brief Buffer with length
 *
 * @details Used to store data of a variable length.
 *
 */
typedef struct
{
    uint16_t len;  /**< Buffer length in bytes */
    uint8_t buf[]; /**< Variable size buffer data */
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

#endif  // BUFFER_H