/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <generated_stow_enums.h>
#include <generated_stow_items.h>
#include <stdint.h>
#include <string.h>
#include <zcbor_decode.h>
#include <zcbor_encode.h>
#include <gantry/memory.h>
#include <gantry/stow/stow.h>
#include <gantry/stow/stow_describe.h>
#include <gantry/stow/types/stow_type_enum.h>
#include <zephyr/ztest.h>
#include <zephyr/ztest_assert.h>

static inline void reset_stow(void* fixture)
{
    (void)fixture;
    stow_init();
}
