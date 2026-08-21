/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gantry/module.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

// Application module
static atomic_t threaded_init_count;
K_SEM_DEFINE(threaded_ran, 0, 1);

static void threaded_module_init(void) { atomic_inc(&threaded_init_count); }

static void threaded_module_thread(void* arg1, void* arg2, void* arg3)
{
    ARG_UNUSED(arg1);
    ARG_UNUSED(arg2);
    ARG_UNUSED(arg3);

    k_sem_give(&threaded_ran);
}

GANTRY_MODULE_DEFINE(test_threaded_module, threaded_module_init, threaded_module_thread, 1024, 5);

// Library module
static atomic_t library_init_count;

static void library_module_init(void) { atomic_inc(&library_init_count); }

GANTRY_LIBRARY_MODULE_DEFINE(test_library_module, library_module_init);

ZTEST_SUITE(gantry_module, NULL, NULL, NULL, NULL, NULL);

ZTEST(gantry_module, test_threaded_module_init_ran_once_at_boot) { zassert_equal(atomic_get(&threaded_init_count), 1); }

ZTEST(gantry_module, test_threaded_module_thread_started_at_boot)
{
    zassert_equal(k_sem_take(&threaded_ran, K_MSEC(100)), 0);
}

ZTEST(gantry_module, test_library_module_init_ran_once_at_boot) { zassert_equal(atomic_get(&library_init_count), 1); }
