/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "test_common.h"

#include <gantry/error.h>
#include <sys/errno.h>

static int resolve(const struct stow_msg_items* items, struct stow_msg_entry* entry)
{
    ARG_UNUSED(items);
    if (entry->id >= STOW_ID_COUNT)
    {
        return -ENOENT;
    }
    entry->local = (uint16_t)entry->id;
    entry->codec = g_stow_const_metadata[entry->id].interface;
    return SUCCESS;
}

const struct stow_msg_items test_items = { .resolve = resolve };
