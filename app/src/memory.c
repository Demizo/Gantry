/**
 * @file memory.c
 * @author Demizo (demizodemazo@gmail.com)
 * @brief Memory module implementation
 * @version 0.1
 * @date 2026-02-28
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "memory.h"

#include <autoconf.h>
#include <stdint.h>
#include <sys/errno.h>
#include <zephyr/logging/log.h>

#include "error.h"
#include "static_unit.h"

LOG_MODULE_REGISTER(mem_mgr, CONFIG_MEM_LOG_LEVEL);

//**********************************************************
//* Local Definitions
//**********************************************************

//**********************************************************
//* Local typedefs
//**********************************************************

typedef struct
{
    struct k_mem_slab* slab;
    size_t block_size;
    size_t block_count;
} mem_pool_metadata_t;

typedef enum
{
    POOL_SMALL = 0,
    POOL_MEDIUM,
    POOL_LARGE,
    POOL_COUNT
} mem_pool_size_t;

// Header stored at the start of each memory block
typedef struct __attribute__((packed))
{
    uint8_t magic[8];      // Magic number for block validation
    mem_pool_size_t pool;  // Pool the block belongs to
    const char* func;      // Function name that allocated the block
    uint32_t ref_count;    // Reference count
} mem_block_header_t;

//**********************************************************
//* Static Function Declarations
//**********************************************************

static void* block_to_header(void* data);
static void* header_to_block(void* block);
STATIC_UNIT mem_block_header_t* validate_and_get_header(void* data);

//**********************************************************
//* Static Variable Definitions
//**********************************************************

static const uint8_t magic[8] = {'M', 'E', 'M', 'B', 'L', 'O', 'C', 'K'};

K_MEM_SLAB_DEFINE_STATIC(small_slab, CONFIG_MEM_SMALL_BLOCK_SIZE, CONFIG_MEM_SMALL_BLOCK_COUNT, sizeof(void*));
K_MEM_SLAB_DEFINE_STATIC(medium_slab, CONFIG_MEM_MEDIUM_BLOCK_SIZE, CONFIG_MEM_MEDIUM_BLOCK_COUNT, sizeof(void*));
K_MEM_SLAB_DEFINE_STATIC(large_slab, CONFIG_MEM_LARGE_BLOCK_SIZE, CONFIG_MEM_LARGE_BLOCK_COUNT, sizeof(void*));

static mem_pool_metadata_t mem_pools[POOL_COUNT] = {
    [POOL_SMALL] =
        {
            .slab = &small_slab,
            .block_size = CONFIG_MEM_SMALL_BLOCK_SIZE,
            .block_count = CONFIG_MEM_SMALL_BLOCK_COUNT,
        },
    [POOL_MEDIUM] =
        {
            .slab = &medium_slab,
            .block_size = CONFIG_MEM_MEDIUM_BLOCK_SIZE,
            .block_count = CONFIG_MEM_MEDIUM_BLOCK_COUNT,
        },
    [POOL_LARGE] =
        {
            .slab = &large_slab,
            .block_size = CONFIG_MEM_LARGE_BLOCK_SIZE,
            .block_count = CONFIG_MEM_LARGE_BLOCK_COUNT,
        },
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

    if (header->pool >= POOL_COUNT)
    {
        return NULL;
    }

    return header;
}

//**********************************************************
//* Public Function Definitions
//**********************************************************

int mem_alloc(size_t size, void** block_ptr, const char* func, const char* file, int line)
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
    for (int i = 0; i < POOL_COUNT; i++)
    {
        mem_pool_metadata_t* pool = &mem_pools[i];
        if (total_size <= pool->block_size)
        {
            mem_pool_size_t pool_size = (mem_pool_size_t)i;
            uint32_t available_blocks = k_mem_slab_num_free_get(pool->slab);
            if (available_blocks == 0)
            {
                if (i == POOL_COUNT - 1)
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
            ret = k_mem_slab_alloc(pool->slab, &block, K_NO_WAIT);
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
            header->func = func;
            header->ref_count = 1;

            *block_ptr = header_to_block(block);

            LOG_DBG("Allocated %zu bytes from pool %d, block: %p, ([%s:%d]: %s)", size, pool_size, *block_ptr,
                    file ? file : "unknown", line, func ? func : "unknown");
            break;
        }
        else
        {
            if (i == POOL_COUNT - 1)
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

int mem_ref(void* block, const char* file, int line)
{
    if (block == NULL)
    {
        LOG_ERR("NULL pointer provided");
        return -EINVAL;
    }

    uint32_t key = irq_lock();

    mem_block_header_t* header = validate_and_get_header(block);
    if (header == NULL || header->ref_count == 0)
    {
        irq_unlock(key);
        LOG_ERR("Invalid memory block");
        return -EINVAL;
    }

    if (header->ref_count == UINT32_MAX)
    {
        irq_unlock(key);
        LOG_ERR("Max references reached");
        return -ENOMEM;
    }

    header->ref_count++;

    LOG_DBG("Referenced block %p (count: %d), allocated by %s ([%s:%d])", block, header->ref_count,
            header->func ? header->func : "unknown", file ? file : "unknown", line);

    irq_unlock(key);

    return SUCCESS;
}

int mem_unref(void** block_ptr, const char* file, int line)
{
    if (block_ptr == NULL)
    {
        LOG_ERR("NULL pointer provided");
        return -EINVAL;
    }

    if (*block_ptr == NULL)
    {
        // Assume that the data is already freed
        return SUCCESS;
    }

    uint32_t key = irq_lock();

    mem_block_header_t* header = validate_and_get_header(*block_ptr);
    if (header == NULL)
    {
        irq_unlock(key);
        LOG_ERR("Invalid memory block");
        return -EINVAL;
    }

    ASSERT(header->ref_count > 0, "Reference count of an allocated block was zero");
    header->ref_count--;

    if (header->ref_count == 0)
    {
        // Free the block when there are no more references
        mem_pool_metadata_t* pool = &mem_pools[header->pool];
        void* block_header = block_to_header(*block_ptr);

        k_mem_slab_free(pool->slab, block_header);
        LOG_DBG("Freed block %p, allocated by %s ([%s:%d])", *block_ptr, header->func ? header->func : "unknown",
                file ? file : "unknown", line);
        *block_ptr = NULL;
    }
    else
    {
        LOG_DBG("Dereferenced block %p (count: %d), allocated by %s ([%s:%d])", *block_ptr, header->ref_count,
                header->func ? header->func : "unknown", file ? file : "unknown", line);
    }

    irq_unlock(key);

    return SUCCESS;
}
