/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Provides the ability to allocate, reference, and dereference blocks of memory.
 *
 * @details Memory blocks are allocated from pools of fixed-size memory blocks (e.g. small, medium, large). The caller
 * can request a buffer size and the smallest available memory block will be returned. Memory blocks are reference
 * counted and start with a reference count of 1 when allocated. When unreferenced, the reference count is decremented.
 * Once the reference count is 0 the block is freed and can be reused.
 *
 * Each memory block has a header that stores a magic value (to identify the block as a valid memory block) and metadata
 * such as the reference count and function that originally allocated it (used for debugging). This header is only used
 * internally by the memory module. From the perspective of the caller, memory blocks are just void pointers. The header
 * metadata is stored at the start of each memory block, but the caller is given a pointer directly after the block
 * header.
 *
 * @author Demizo (demizodemazo@gmail.com)
 *
 *
 */

#pragma once

#include <stddef.h>
#include <zephyr/kernel.h>

/**
 * @defgroup data_management Data Management
 *
 * @brief Data is shared by passing around reference counted events.
 *
 * @details For how memory is allocated, see @ref memory.h. For how events are used, see @ref event.h.
 *
 * @{
 */

//**********************************************************
//* Definitions
//**********************************************************

#ifdef CONFIG_MEM_TRACE
typedef struct
{
    const char* func;
    const char* file;
    int line;
} mem_trace_info_t;

extern volatile mem_trace_info_t g_current_mem_trace;

#define TRACE_WRAP(func_call)                                                     \
    ({                                                                            \
        mem_trace_info_t _prev;                                                   \
        unsigned int _key = irq_lock();                                           \
        _prev = g_current_mem_trace;                                              \
        g_current_mem_trace = (mem_trace_info_t){ __func__, __FILE__, __LINE__ }; \
        irq_unlock(_key);                                                         \
        __auto_type _ret = (func_call);                                           \
        _key = irq_lock();                                                        \
        g_current_mem_trace = _prev;                                              \
        irq_unlock(_key);                                                         \
        _ret;                                                                     \
    })

#define TRACE_WRAP_VOID(func_call)                                                \
    do                                                                            \
    {                                                                             \
        mem_trace_info_t _prev;                                                   \
        unsigned int _key = irq_lock();                                           \
        _prev = g_current_mem_trace;                                              \
        g_current_mem_trace = (mem_trace_info_t){ __func__, __FILE__, __LINE__ }; \
        irq_unlock(_key);                                                         \
        (func_call);                                                              \
        _key = irq_lock();                                                        \
        g_current_mem_trace = _prev;                                              \
        irq_unlock(_key);                                                         \
    } while (0)

#else

/**
 * @brief Call a function with the current location as the memory trace
 *
 * @details This macro sets and restores the trace in a threadsafe manner.
 * It can be used in ISR contexts. When tracing is disabled this wrapper has no effect.
 */
#define TRACE_WRAP(func_call) (func_call)

/**
 * @brief Call a function with the current location as the memory trace
 *
 * @details Variant of @ref TRACE_WRAP for functions that have no return value.
 */
#define TRACE_WRAP_VOID(func_call) (func_call)

#endif

//**********************************************************
//* Typedefs, Enums, and Structs
//**********************************************************

//**********************************************************
//* Functions
//**********************************************************

/**
 * @brief Allocate a block of memory
 *
 * @details The block will be allocated from a memory pool based on the requested size. The block will be provided from
 * the smallest possible memory pool capable of holding the requested size.
 *
 * @note It is recommended that you always use the MEM_ALLOC macro which calls this function with the debug information
 * filled in.
 *
 * @param[in] size The requested buffer size in bytes
 * @param[out] block_ptr Pointer to be populated with the address of memory block. This pointer must point to a NULL
 * pointer when this function is called. It is only populated when the allocation succeeds.
 *
 * @return SUCCESS when the allocation is successful.
 * @return -EINVAL the provided pointer was NULL, a size of 0 was requested, or the requested size was larger than the
 * maximum block size.
 * @return -ENOTEMPTY the block pointer was not pointing to a NULL pointer.
 * @return -ENOMEM No blocks were available that could fit the requested size.
 */
int mem_alloc(size_t size, void** block_ptr);

/**
 * @brief Convenience macro for @ref mem_alloc with memory tracing
 */
#define MEM_ALLOC(size, data) TRACE_WRAP(mem_alloc(size, data))

/**
 * @brief Increment the reference count of a memory block
 *
 * @note It is recommended that you always use the MEM_REF macro which calls this function with the debug
 * information filled in.
 *
 * @param[in] block The memory block pointer to be reference counted
 */
void mem_ref(void* block);

/**
 * @brief Convenience macro for @ref mem_ref with memory tracing
 */
#define MEM_REF(data) TRACE_WRAP_VOID(mem_ref(data))

/**
 * @brief Dencrement the reference count of a memory block
 *
 * @note It is recommended that you always use the MEM_UNREF macro which calls this function with the debug
 * information filled in.
 *
 * @note If block_ptr already points to a NULL pointer, this function will assume that the block was already freed and
 * do nothing.
 *
 * @param[in,out] block_ptr A pointer to the memory block pointer to be dereferenced. If the reference count reaches 0,
 * the block pointer will be set to NULL.
 */
void mem_unref(void** block_ptr);

/**
 * @brief Convenience macro for @ref mem_unref with memory tracing
 */
#define MEM_UNREF(data) TRACE_WRAP_VOID(mem_unref(data))

/**
 * @brief Get the current reference count for a memory block
 *
 * @param block The memory block to get the reference count of
 *
 * @return The current reference count of the provided memory block
 */
uint32_t mem_get_ref_count(void* block);

/**
 * @brief Get the number of active memory pools
 *
 * @return The number of configured memory pools
 */
uint8_t mem_get_pool_count(void);

/**
 * @brief Get the usage of a memory pool
 *
 * @param[in] pool_index The pool index (e.g. POOL1 = 0)
 * @param[out] used_out Number of currently allocated blocks
 * @param[out] total_out Total number of blocks in the pool
 *
 * @return SUCCESS on success
 * @return -EINVAL if pool_index is out of range or either output pointer is NULL
 */
int mem_get_pool_usage(uint8_t pool_index, uint32_t* used_out, uint32_t* total_out);

#ifdef CONFIG_MEM_WATERMARK
/**
 * @brief Callback type for pool watermark notifications
 *
 * @param pool_index The pool index that hit the watermark
 * @param percent The watermark percentage that was hit
 */
typedef void (*mem_watermark_cb_t)(uint8_t pool_index, uint8_t percent);

/**
 * @brief Register a watermark callback for a memory pool
 *
 * @details The callback is invoked the first time pool usage reaches or exceeds the given
 * percentage. It will not fire again unless mem_set_watermark is called again.
 *
 * @param[in] pool_index The pool index (e.g. POOL1 = 0)
 * @param[in] percent Usage percentage threshold (0-100)
 * @param[in] callback Function to call when the watermark is first reached
 *
 * @return SUCCESS on success
 * @return -EINVAL if pool_index is out of range, percent > 100, or callback is NULL
 */
int mem_set_watermark(uint8_t pool_index, uint8_t percent, mem_watermark_cb_t callback);
#endif

/**
 * @brief Indicates to static analysis that ownership over the memory will be the responsibility of the caller
 */
#define PASS_OWNERSHIP(data)

/**
 * @brief Indicates to static analysis that the memory was not allocated or referenced
 */
#define NOT_REFERENCED(data)

/**
 * @}
 */
