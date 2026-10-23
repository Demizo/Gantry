/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <gantry/stow/stow_msg.h>
#include <generated_stow_items.h>
#include <zephyr/ztest.h>

/**
 * @brief Resolver of the fixture's items, whose wire IDs are their fixture IDs
 */
extern const struct stow_msg_items test_items;
