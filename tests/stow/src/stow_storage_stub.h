/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <generated_stow_items.h>

#define STOW_STORAGE_STUB_MAX_SAVES 16

/**
 * @brief Record of what the stubbed persistent storage backend was asked to save
 */
struct stow_storage_stub
{
    int save_count;                                  /**< Total saves requested */
    uint32_t saved_ids[STOW_STORAGE_STUB_MAX_SAVES]; /**< Item IDs saved, oldest first */
    void (*during_save)(void);                       /**< Runs once, inside the next save */
    int next_save_result;                            /**< Returned by the next save, then cleared */
};

extern struct stow_storage_stub g_stow_storage_stub;

/**
 * @brief Forget every recorded save and clear the during_save hook
 */
void stow_storage_stub_reset(void);

/**
 * @brief Count how many times an item was saved
 */
int stow_storage_stub_item_save_count(enum stow_item_id id);
