/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

/**
 * @brief One message of an exchange
 */
struct protocol_step
{
    uint8_t side;        /**< Index into the sides; 0 is the first declared side */
    const uint8_t* data; /**< Encoded message */
    size_t len;          /**< Length of data */
    const char* edn;     /**< The message in diagnostic notation, or NULL when written as raw bytes */
};

/**
 * @brief A golden vector: a script of messages between two sides on one fresh session
 */
struct protocol_vector
{
    const char* name;                  /**< Vector name */
    const char* meaning;               /**< What the vector shows */
    const char* const* roles;          /**< Role names of the session, NULL-terminated */
    const struct protocol_step* steps; /**< Messages in order */
    size_t step_count;                 /**< Number of steps */
};
