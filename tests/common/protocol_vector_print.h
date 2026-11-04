/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <zephyr/sys/printk.h>

/**
 * @brief Print the bytes a side sent as one hex string, which `protocol-spec diag` writes as a step
 *
 * @param label What the bytes are, such as "sent"
 * @param data The bytes
 * @param len Number of bytes
 */
static inline void protocol_vector_print_hex(const char* label, const uint8_t* data, size_t len)
{
    printk("%s: ", label);
    for (size_t i = 0; i < len; i++)
    {
        printk("%02x", data[i]);
    }
    printk("\n");
}
