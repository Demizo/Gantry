/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "stow_storage_stub.h"

#include <gantry/error.h>
#include <gantry/stow/stow_storage.h>
#include <zephyr/sys/util.h>

struct stow_storage_stub g_stow_storage_stub;

int stow_storage_load(void) { return SUCCESS; }

int stow_storage_save_item(const struct stow_item_const_metadata* item)
{
    if (g_stow_storage_stub.save_count < STOW_STORAGE_STUB_MAX_SAVES)
    {
        g_stow_storage_stub.saved_ids[g_stow_storage_stub.save_count] = item->id;
    }
    g_stow_storage_stub.save_count++;

    // Stands in for a write landing while the real backend is busy in flash.
    void (*hook)(void) = g_stow_storage_stub.during_save;
    if (hook != NULL)
    {
        g_stow_storage_stub.during_save = NULL;
        hook();
    }

    int result = g_stow_storage_stub.next_save_result;
    g_stow_storage_stub.next_save_result = SUCCESS;
    return result;
}

void stow_storage_stub_reset(void) { g_stow_storage_stub = (struct stow_storage_stub){ 0 }; }

int stow_storage_stub_item_save_count(enum stow_item_id id)
{
    int count = 0;
    int recorded = MIN(g_stow_storage_stub.save_count, STOW_STORAGE_STUB_MAX_SAVES);

    for (int i = 0; i < recorded; i++)
    {
        if (g_stow_storage_stub.saved_ids[i] == (uint32_t)id)
        {
            count++;
        }
    }

    return count;
}
