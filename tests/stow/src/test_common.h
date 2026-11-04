/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <gantry/memory.h>
#include <gantry/stow/stow.h>
#include <gantry/stow/types/stow_type_enum.h>
#include <generated_stow_enums.h>
#include <generated_stow_items.h>
#include <stdint.h>
#include <string.h>
#include <zcbor_decode.h>
#include <zcbor_encode.h>
#include <zephyr/ztest.h>
#include <zephyr/ztest_assert.h>

#include "stow_storage_stub.h"

static inline void reset_stow(void* fixture)
{
    (void)fixture;
    stow_init();
    stow_storage_stub_reset();
}

static inline uint32_t blocks_in_use(void)
{
    uint32_t total_used = 0;
    for (uint8_t pool = 0; pool < CONFIG_MEM_POOL_COUNT; pool++)
    {
        uint32_t used = 0;
        uint32_t total = 0;
        (void)mem_get_pool_usage(pool, &used, &total);
        total_used += used;
    }
    return total_used;
}
