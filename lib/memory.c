/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @author Demizo (demizodemazo@gmail.com)
 * @brief Memory module implementation
 *
 *
 */

#include <autoconf.h>
#include <stdint.h>
#include <sys/errno.h>
#include <gantry/error.h>
#include <gantry/memory.h>
#include <gantry/static_unit.h>
#include <zephyr/logging/log.h>

/**
 * @brief Logger for module
 */
LOG_MODULE_REGISTER(mem_mgr, CONFIG_MEM_LOG_LEVEL);

//**********************************************************
//* Local Definitions
//**********************************************************

#ifdef CONFIG_MEM_TRACE
volatile mem_trace_info_t g_current_mem_trace = { 0 };
#endif

//**********************************************************
//* Local typedefs
//**********************************************************

/**
 * @brief Metadata for a memory pool
 */
typedef struct
{
    struct k_mem_slab* slab; /**< The pool's memory slab */
    size_t block_size;       /**< The size of each block in the pool */
    size_t block_count;      /**< The number of blocks in the pool */
} mem_pool_metadata_t;

/**
 * @brief Header stored at the start of each memory block
 */
typedef struct
{
    uint8_t magic[8]; /**< Magic number for block validation */
    uint8_t pool;     /**< Pool the block belongs to */
#ifdef CONFIG_MEM_TRACE
    const char* func; /**< Function name that allocated the block */
#endif
    uint32_t ref_count; /**< Reference count */
} __attribute__((aligned(sizeof(void*)))) mem_block_header_t;

#ifdef CONFIG_MEM_WATERMARK
/**
 * @brief Per-pool watermark configuration
 */
typedef struct
{
    mem_watermark_cb_t callback;
    uint8_t percent;
    bool triggered;
} mem_watermark_t;
#endif

//**********************************************************
//* Static Function Declarations
//**********************************************************

static void* block_to_header(void* data);
static void* header_to_block(void* block);
STATIC_UNIT mem_block_header_t* validate_and_get_header(void* data);

//**********************************************************
//* Static Variable Definitions
//**********************************************************

static const uint8_t magic[8] = { 'M', 'E', 'M', 'B', 'L', 'O', 'C', 'K' };

#ifdef CONFIG_MEM_WATERMARK
static mem_watermark_t watermarks[CONFIG_MEM_POOL_COUNT];
#endif

/**
 * @brief Pool 1 slab
 */
K_MEM_SLAB_DEFINE_STATIC(
    pool1_slab, CONFIG_MEM_POOL1_BLOCK_SIZE + sizeof(mem_block_header_t), CONFIG_MEM_POOL1_BLOCK_COUNT, sizeof(void*));
#if CONFIG_MEM_POOL_COUNT >= 2
/**
 * @brief Pool 2 slab
 */
K_MEM_SLAB_DEFINE_STATIC(
    pool2_slab, CONFIG_MEM_POOL2_BLOCK_SIZE + sizeof(mem_block_header_t), CONFIG_MEM_POOL2_BLOCK_COUNT, sizeof(void*));
#endif
#if CONFIG_MEM_POOL_COUNT >= 3
/**
 * @brief Pool 3 slab
 */
K_MEM_SLAB_DEFINE_STATIC(
    pool3_slab, CONFIG_MEM_POOL3_BLOCK_SIZE + sizeof(mem_block_header_t), CONFIG_MEM_POOL3_BLOCK_COUNT, sizeof(void*));
#endif
#if CONFIG_MEM_POOL_COUNT >= 4
/**
 * @brief Pool 4 slab
 */
K_MEM_SLAB_DEFINE_STATIC(
    pool4_slab, CONFIG_MEM_POOL4_BLOCK_SIZE + sizeof(mem_block_header_t), CONFIG_MEM_POOL4_BLOCK_COUNT, sizeof(void*));
#endif
#if CONFIG_MEM_POOL_COUNT >= 5
/**
 * @brief Pool 5 slab
 */
K_MEM_SLAB_DEFINE_STATIC(
    pool5_slab, CONFIG_MEM_POOL5_BLOCK_SIZE + sizeof(mem_block_header_t), CONFIG_MEM_POOL5_BLOCK_COUNT, sizeof(void*));
#endif
#if CONFIG_MEM_POOL_COUNT >= 6
/**
 * @brief Pool 6 slab
 */
K_MEM_SLAB_DEFINE_STATIC(
    pool6_slab, CONFIG_MEM_POOL6_BLOCK_SIZE + sizeof(mem_block_header_t), CONFIG_MEM_POOL6_BLOCK_COUNT, sizeof(void*));
#endif

static struct k_mem_slab* mem_pools[CONFIG_MEM_POOL_COUNT] = {
    &pool1_slab,
#if CONFIG_MEM_POOL_COUNT >= 2
    &pool2_slab,
#endif
#if CONFIG_MEM_POOL_COUNT >= 3
    &pool3_slab,
#endif
#if CONFIG_MEM_POOL_COUNT >= 4
    &pool4_slab,
#endif
#if CONFIG_MEM_POOL_COUNT >= 5
    &pool5_slab,
#endif
#if CONFIG_MEM_POOL_COUNT >= 6
    &pool6_slab,
#endif
};

//**********************************************************
//* Static Function Definitions
//**********************************************************

/**
 * @brief Convert a block pointer to a block header pointer
 *
 * @param block block pointer
 * @return header pointer for the provided block
 */
static void* block_to_header(void* block) { return (uint8_t*)block - sizeof(mem_block_header_t); }

/**
 * @brief Get block pointer from a block header pointer
 *
 * @param header pointer to a block header
 * @return block pointer for the provided header
 */
static void* header_to_block(void* header) { return (uint8_t*)header + sizeof(mem_block_header_t); }

/**
 * @brief Validate that the block pointer points to a valid memory block and
 * return the header
 *
 * @param block block pointer
 * @return pointer to the block header if the block was valid, otherwise NULL
 */
STATIC_UNIT mem_block_header_t* validate_and_get_header(void* block)
{
    if (block == NULL) return NULL;

    mem_block_header_t* header = (mem_block_header_t*)block_to_header(block);

    // Check magic value
    if (memcmp(header->magic, magic, sizeof(magic)) != 0)
    {
        return NULL;
    }

    if (header->pool >= CONFIG_MEM_POOL_COUNT)
    {
        return NULL;
    }

    return header;
}

//**********************************************************
//* Public Function Definitions
//**********************************************************

int mem_alloc(size_t size, void** block_ptr)
{
    int ret = SUCCESS;
    void* block = NULL;

    if (block_ptr == NULL)
    {
        LOG_ERR("NULL pointer provided");
        return -EINVAL;
    }

    if (*block_ptr != NULL)
    {
        LOG_ERR("Block pointer to populate is not NULL");
        return -ENOTEMPTY;
    }

    if (size == 0)
    {
        LOG_ERR("Requested an empty block");
        return -EINVAL;
    }

    // The total size must include room for the block header alongside the
    // requested size
    size_t total_size = size + sizeof(mem_block_header_t);

    uint32_t key = irq_lock();

    // Find the smallest possible pool that can fit the requested size
    for (int i = 0; i < CONFIG_MEM_POOL_COUNT; i++)
    {
        struct k_mem_slab* pool = mem_pools[i];
        if (total_size <= pool->info.block_size)
        {
            uint8_t pool_size = (uint8_t)i;
            uint32_t available_blocks = k_mem_slab_num_free_get(pool);
            if (available_blocks == 0)
            {
                if (i == CONFIG_MEM_POOL_COUNT - 1)
                {
                    // No more pools to check
                    LOG_ERR("No memory blocks found for size %zu", size);
                    irq_unlock(key);
                    return -ENOMEM;
                }
                else
                {
                    // Check the next pool
                    continue;
                }
            }

            // Allocate block
            ret = k_mem_slab_alloc(pool, &block, K_NO_WAIT);
            if (ret)
            {
                irq_unlock(key);
                LOG_WRN("Failed to allocate from pool %d: %d", pool_size, ret);
                return -ENOMEM;
            }

            // Initialize block
            mem_block_header_t* header = (mem_block_header_t*)block;
            memcpy(header->magic, magic, sizeof(magic));
            header->pool = pool_size;
#ifdef CONFIG_MEM_TRACE
            header->func = g_current_mem_trace.func;
#endif
            header->ref_count = 1;

            *block_ptr = header_to_block(block);

#ifdef CONFIG_MEM_TRACE
            LOG_DBG(
                "Allocated %zu bytes from pool %d, block: %p, [%s:%d]: %s", size, pool_size, *block_ptr,
                g_current_mem_trace.file ? g_current_mem_trace.file : "unknown", g_current_mem_trace.line,
                g_current_mem_trace.func ? g_current_mem_trace.func : "unknown");
#endif

#ifdef CONFIG_MEM_WATERMARK
            // Check watermark for this pool
            mem_watermark_t* wm = &watermarks[pool_size];
            if (wm->callback != NULL && !wm->triggered)
            {
                uint32_t used = k_mem_slab_num_used_get(pool);
                uint32_t total = pool->info.num_blocks;
                uint32_t usage_percent = (used * 100u) / total;
                if (usage_percent >= wm->percent)
                {
                    wm->triggered = true;
                    mem_watermark_cb_t cb = wm->callback;
                    uint8_t wm_percent = wm->percent;
                    irq_unlock(key);
                    cb(pool_size, wm_percent);
                    key = irq_lock();
                }
            }
#endif

            break;
        }
        else
        {
            if (i == CONFIG_MEM_POOL_COUNT - 1)
            {
                LOG_ERR("Requested size %zu exceeds the maximum block size", size);
                irq_unlock(key);
                return -EINVAL;
            }
        }
    }

    irq_unlock(key);

    return SUCCESS;
}

void mem_ref(void* block)
{
    uint32_t key = irq_lock();

    mem_block_header_t* header = validate_and_get_header(block);
    ASSERT(((header != NULL) && header->ref_count > 0), "Invalid memory block");
    ASSERT((header->ref_count < UINT32_MAX), "Max references reached");

    header->ref_count++;

#ifdef CONFIG_MEM_TRACE
    LOG_DBG(
        "Referenced block %p (count: %d), allocated by %s [%s:%d]", block, header->ref_count,
        header->func ? header->func : "unknown", g_current_mem_trace.file ? g_current_mem_trace.file : "unknown",
        g_current_mem_trace.line);
#endif

    irq_unlock(key);
}

void mem_unref(void** block_ptr)
{
    ASSERT(block_ptr != NULL, "Invalid memory block");

    if (*block_ptr == NULL)
    {
        // Assume that the data is already freed
        return;
    }

    uint32_t key = irq_lock();

    mem_block_header_t* header = validate_and_get_header(*block_ptr);
    ASSERT(((header != NULL) && header->ref_count > 0), "Invalid memory block");

    header->ref_count--;

    if (header->ref_count == 0)
    {
        // Free the block when there are no more references
        struct k_mem_slab* pool = mem_pools[header->pool];
        void* block_header = block_to_header(*block_ptr);

        k_mem_slab_free(pool, block_header);
#ifdef CONFIG_MEM_TRACE
        LOG_DBG(
            "Freed block %p, allocated by %s [%s:%d]", *block_ptr, header->func ? header->func : "unknown",
            g_current_mem_trace.file ? g_current_mem_trace.file : "unknown", g_current_mem_trace.line);
#endif
        *block_ptr = NULL;
    }
    else
    {
#ifdef CONFIG_MEM_TRACE
        LOG_DBG(
            "Dereferenced block %p (count: %d), allocated by %s [%s:%d]", *block_ptr, header->ref_count,
            header->func ? header->func : "unknown", g_current_mem_trace.file ? g_current_mem_trace.file : "unknown",
            g_current_mem_trace.line);
#endif
    }

    irq_unlock(key);
}

uint32_t mem_get_ref_count(void* block)
{
    uint32_t key = irq_lock();

    mem_block_header_t* header = validate_and_get_header(block);
    ASSERT(header != NULL, "Invalid memory block");
    uint32_t current_count = header->ref_count;

    irq_unlock(key);

    return current_count;
}

uint8_t mem_get_pool_count(void) { return CONFIG_MEM_POOL_COUNT; }

int mem_get_pool_usage(uint8_t pool_index, uint32_t* used_out, uint32_t* total_out)
{
    if (pool_index >= CONFIG_MEM_POOL_COUNT || used_out == NULL || total_out == NULL)
    {
        return -EINVAL;
    }

    uint32_t key = irq_lock();

    struct k_mem_slab* pool = mem_pools[pool_index];
    *used_out = k_mem_slab_num_used_get(pool);
    *total_out = pool->info.num_blocks;

    irq_unlock(key);

    return SUCCESS;
}

#ifdef CONFIG_MEM_WATERMARK
int mem_set_watermark(uint8_t pool_index, uint8_t percent, mem_watermark_cb_t callback)
{
    if (pool_index >= CONFIG_MEM_POOL_COUNT || percent > 100 || callback == NULL)
    {
        return -EINVAL;
    }

    uint32_t key = irq_lock();

    watermarks[pool_index].callback = callback;
    watermarks[pool_index].percent = percent;
    watermarks[pool_index].triggered = false;

    irq_unlock(key);

    return SUCCESS;
}
#endif
