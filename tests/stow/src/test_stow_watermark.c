/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gantry/error.h>

#include "test_common.h"

static volatile int g_watermark_call_count;
static volatile bool g_watermark_saw_isr;
static volatile bool g_watermark_saw_irq_locked;

static void watermark_cb(uint8_t pool_index, uint8_t percent)
{
    ARG_UNUSED(pool_index);
    ARG_UNUSED(percent);

    g_watermark_saw_isr = k_is_in_isr();

    unsigned int key = irq_lock();
    g_watermark_saw_irq_locked = !arch_irq_unlocked(key);
    irq_unlock(key);

    g_watermark_call_count++;
}

static void copy_subscriber_cb(event_t* event) { ARG_UNUSED(event); }

static void reset_watermark_test(void* fixture)
{
    reset_stow(fixture);
    g_watermark_call_count = 0;
    g_watermark_saw_isr = true;
    g_watermark_saw_irq_locked = true;
}

ZTEST_SUITE(stow_watermark, NULL, NULL, reset_watermark_test, NULL, NULL);

ZTEST(stow_watermark, test_watermark_during_notify_runs_in_thread_context)
{
    struct stow_subscription sub = {
        .mode = STOW_SUBSCRIPTION_COPY,
        .cb = copy_subscriber_cb,
    };
    zassert_equal(stow_subscribe(STOW_ID_TEST_INT, &sub), SUCCESS);

    // Arm every pool at a 1% threshold so whichever pool notify_subscribers()'s EVENT_ALLOC call
    // lands in, the very first allocation after arming trips it.
    for (uint8_t pool = 0; pool < mem_get_pool_count(); pool++)
    {
        zassert_equal(mem_set_watermark(pool, 1, watermark_cb), SUCCESS);
    }

    data_value_t val = { .type = STOW_ITEM_TYPE_INT, .data.int_value = 9 };
    zassert_equal(stow_set(STOW_ID_TEST_INT, val), SUCCESS);

    k_yield();

    zassert_true(g_watermark_call_count > 0, "Watermark callback never ran; test is not exercising the intended path");
    zassert_false(g_watermark_saw_isr, "Watermark callback ran in interrupt context");
    zassert_false(g_watermark_saw_irq_locked, "Watermark callback ran with interrupts locked");

    stow_unsubscribe(STOW_ID_TEST_INT, &sub);
}
