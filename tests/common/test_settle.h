/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <zephyr/kernel.h>

/**
 * @brief Let every thread above the lowest application priority run until it waits, which takes in every Stow thread
 * in the build, without sending it anything
 */
static inline void test_settle(void)
{
    k_tid_t self = k_current_get();
    int priority = k_thread_priority_get(self);
    k_thread_priority_set(self, K_LOWEST_APPLICATION_THREAD_PRIO);
    k_thread_priority_set(self, priority);
}
