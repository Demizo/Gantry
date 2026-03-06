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
    uint16_t len; /**< Buffer length in bytes */
    void* buf;    /**< Pointer to buffer data */
} buffer_t;

#endif  // BUFFER_H