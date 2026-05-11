/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zds/error.h>
#include <zds/static_unit.h>
#include <zephyr/ztest.h>

#include "memory.c"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static void free_all_blocks(void** blocks, int count)
{
    for (int i = 0; i < count; i++)
    {
        if (blocks[i] != NULL)
        {
            MEM_UNREF(&blocks[i]);
        }
    }
}

// ---------------------------------------------------------------------------
// Suite: mem_alloc
// ---------------------------------------------------------------------------

ZTEST_SUITE(mem_alloc, NULL, NULL, NULL, NULL, NULL);

ZTEST(mem_alloc, test_alloc_basic)
{
    void* block = NULL;
    int ret = MEM_ALLOC(8, &block);
    zassert_equal(ret, 0);
    zassert_not_null(block);
    MEM_UNREF(&block);
}

ZTEST(mem_alloc, test_alloc_null_block_ptr)
{
    int ret = MEM_ALLOC(8, NULL);
    zassert_equal(ret, -EINVAL);
}

ZTEST(mem_alloc, test_alloc_non_null_block_ptr)
{
    void* block = NULL;
    MEM_ALLOC(8, &block);
    zassert_not_null(block);

    int ret = MEM_ALLOC(8, &block);
    zassert_equal(ret, -ENOTEMPTY);
    MEM_UNREF(&block);
}

ZTEST(mem_alloc, test_alloc_zero_size)
{
    void* block = NULL;
    int ret = MEM_ALLOC(0, &block);
    zassert_equal(ret, -EINVAL);
}

ZTEST(mem_alloc, test_alloc_too_large)
{
    void* block = NULL;
    int ret = MEM_ALLOC(4096, &block);
    zassert_equal(ret, -EINVAL);
}

ZTEST(mem_alloc, test_alloc_selects_smallest_pool)
{
    uint32_t used_before, total, used_after;
    mem_get_pool_usage(0, &used_before, &total);

    void* block = NULL;
    MEM_ALLOC(8, &block);  // fits in pool 0 (16 bytes)
    zassert_not_null(block);

    mem_get_pool_usage(0, &used_after, &total);
    zassert_equal(used_after, used_before + 1);

    MEM_UNREF(&block);
}

ZTEST(mem_alloc, test_alloc_exhausted)
{
    // Exhaust all pools for size <= 16 bytes
    // Pool 0 has 4 blocks, pool 1 has 2, pool 2 has 1: total 7 for small sizes
    void* blocks[8] = { 0 };
    int count = 0;
    int ret = 0;

    while (ret == 0 && count < (int)ARRAY_SIZE(blocks))
    {
        ret = MEM_ALLOC(8, &blocks[count]);
        if (ret == 0) count++;
    }

    zassert_equal(ret, -ENOMEM);
    free_all_blocks(blocks, count);
}

// ---------------------------------------------------------------------------
// Suite: mem_ref_unref
// ---------------------------------------------------------------------------

ZTEST_SUITE(mem_ref_unref, NULL, NULL, NULL, NULL, NULL);

ZTEST(mem_ref_unref, test_ref_increments_count)
{
    void* block = NULL;
    MEM_ALLOC(8, &block);
    zassert_equal(mem_get_ref_count(block), 1);

    MEM_REF(block);
    zassert_equal(mem_get_ref_count(block), 2);

    MEM_UNREF(&block);
    MEM_UNREF(&block);
}

ZTEST(mem_ref_unref, test_unref_decrements_count)
{
    void* block = NULL;
    MEM_ALLOC(8, &block);
    MEM_REF(block);
    zassert_equal(mem_get_ref_count(block), 2);

    MEM_UNREF(&block);
    zassert_equal(mem_get_ref_count(block), 1);
    MEM_UNREF(&block);
}

ZTEST(mem_ref_unref, test_unref_frees_at_zero)
{
    void* block = NULL;
    MEM_ALLOC(8, &block);
    MEM_UNREF(&block);
    zassert_is_null(block);
}

ZTEST(mem_ref_unref, test_unref_null_ptr_no_op)
{
    void* block = NULL;
    // Should not assert or crash
    MEM_UNREF(&block);
    zassert_is_null(block);
}

// ---------------------------------------------------------------------------
// Suite: mem_pool_info
// ---------------------------------------------------------------------------

ZTEST_SUITE(mem_pool_info, NULL, NULL, NULL, NULL, NULL);

ZTEST(mem_pool_info, test_get_pool_count) { zassert_equal(mem_get_pool_count(), CONFIG_MEM_POOL_COUNT); }

ZTEST(mem_pool_info, test_get_pool_usage_initial)
{
    uint32_t used, total;
    int ret = mem_get_pool_usage(0, &used, &total);
    zassert_equal(ret, 0);
    zassert_equal(used, 0);
    zassert_equal(total, CONFIG_MEM_POOL1_BLOCK_COUNT);
}

ZTEST(mem_pool_info, test_get_pool_usage_after_alloc)
{
    uint32_t used, total;
    void* block = NULL;

    MEM_ALLOC(8, &block);
    mem_get_pool_usage(0, &used, &total);
    zassert_equal(used, 1);

    MEM_UNREF(&block);
    mem_get_pool_usage(0, &used, &total);
    zassert_equal(used, 0);
}

ZTEST(mem_pool_info, test_get_pool_usage_invalid_pool)
{
    uint32_t used, total;
    int ret = mem_get_pool_usage(CONFIG_MEM_POOL_COUNT, &used, &total);
    zassert_equal(ret, -EINVAL);
}

ZTEST(mem_pool_info, test_get_pool_usage_null_out)
{
    uint32_t used, total;
    zassert_equal(mem_get_pool_usage(0, NULL, &total), -EINVAL);
    zassert_equal(mem_get_pool_usage(0, &used, NULL), -EINVAL);
}

// ---------------------------------------------------------------------------
// Suite: mem_watermark
// ---------------------------------------------------------------------------

static volatile int watermark_call_count;
static volatile uint8_t watermark_last_pool;
static volatile uint8_t watermark_last_percent;

static void watermark_cb(uint8_t pool_index, uint8_t percent)
{
    watermark_call_count++;
    watermark_last_pool = pool_index;
    watermark_last_percent = percent;
}

static void watermark_before(void* fixture)
{
    ARG_UNUSED(fixture);
    watermark_call_count = 0;
    watermark_last_pool = 0xFF;
    watermark_last_percent = 0xFF;
}

ZTEST_SUITE(mem_watermark, NULL, NULL, watermark_before, NULL, NULL);

ZTEST(mem_watermark, test_watermark_fires_at_threshold)
{
    // Pool 0 has 4 blocks. Set watermark at 50% (2/4 blocks).
    mem_set_watermark(0, 50, watermark_cb);

    void* b1 = NULL;
    void* b2 = NULL;
    MEM_ALLOC(8, &b1);
    zassert_equal(watermark_call_count, 0);

    MEM_ALLOC(8, &b2);  // 2/4 = 50%: watermark should fire
    zassert_equal(watermark_call_count, 1);
    zassert_equal(watermark_last_pool, 0);
    zassert_equal(watermark_last_percent, 50);

    MEM_UNREF(&b1);
    MEM_UNREF(&b2);
}

ZTEST(mem_watermark, test_watermark_fires_only_once)
{
    mem_set_watermark(0, 25, watermark_cb);  // 25% of 4 = 1 block

    void* blocks[4] = { 0 };
    for (int i = 0; i < 4; i++)
    {
        MEM_ALLOC(8, &blocks[i]);
    }

    zassert_equal(watermark_call_count, 1);
    free_all_blocks(blocks, 4);
}

ZTEST(mem_watermark, test_watermark_invalid_args)
{
    zassert_equal(mem_set_watermark(CONFIG_MEM_POOL_COUNT, 50, watermark_cb), -EINVAL);
    zassert_equal(mem_set_watermark(0, 101, watermark_cb), -EINVAL);
    zassert_equal(mem_set_watermark(0, 50, NULL), -EINVAL);
}

ZTEST(mem_watermark, test_watermark_reset_on_re_register)
{
    // Trigger the watermark once
    mem_set_watermark(0, 25, watermark_cb);
    void* b1 = NULL;
    MEM_ALLOC(8, &b1);
    zassert_equal(watermark_call_count, 1);
    MEM_UNREF(&b1);

    // Re-register to reset triggered state
    mem_set_watermark(0, 25, watermark_cb);
    void* b2 = NULL;
    MEM_ALLOC(8, &b2);
    zassert_equal(watermark_call_count, 2);
    MEM_UNREF(&b2);
}

ZTEST(mem_watermark, test_watermark_does_not_re_fire)
{
    // Trigger the watermark once
    mem_set_watermark(0, 25, watermark_cb);
    void* b1 = NULL;
    MEM_ALLOC(8, &b1);
    zassert_equal(watermark_call_count, 1);

    // Go below watermark
    MEM_UNREF(&b1);

    // Attempt to trigger watermark again
    MEM_ALLOC(8, &b1);
    zassert_equal(watermark_call_count, 1);
    MEM_UNREF(&b1);
}

// ---------------------------------------------------------------------------
// Suite: mem_internal (validate_and_get_header)
// ---------------------------------------------------------------------------

ZTEST_SUITE(mem_internal, NULL, NULL, NULL, NULL, NULL);

ZTEST(mem_internal, test_validate_valid_block)
{
    void* block = NULL;
    MEM_ALLOC(8, &block);
    zassert_not_null(validate_and_get_header(block));
    MEM_UNREF(&block);
}

ZTEST(mem_internal, test_validate_null_block) { zassert_is_null(validate_and_get_header(NULL)); }

ZTEST(mem_internal, test_validate_corrupted_magic)
{
    void* block = NULL;
    MEM_ALLOC(8, &block);

    // Retrieve the header pointer, then corrupt its first magic byte
    mem_block_header_t* header = validate_and_get_header(block);
    zassert_not_null(header);
    uint8_t saved = header->magic[0];
    header->magic[0] = 0x00;

    zassert_is_null(validate_and_get_header(block));

    // Restore magic so the block can be freed cleanly
    header->magic[0] = saved;
    MEM_UNREF(&block);
}
