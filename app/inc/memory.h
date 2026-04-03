/**
 * @file memory.h
 *
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
 * @version 0.1
 * @date 2026-02-28
 *
 * @copyright Copyright (c) 2026
 *
 */

#ifndef MEMORY_H
#define MEMORY_H

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
 * @brief Increment the reference count of a memory block
 *
 * @note It is recommended that you always use the MEM_REF macro which calls this function with the debug
 * information filled in.
 *
 * @param[in] block The memory block pointer to be reference counted
 *
 * @return SUCCESS when the block's reference count was incremented
 * @return -EINVAL when the provided pointer was not a valid memory block
 * @return -ENOMEM when the block already has the maximum number of references
 */
int mem_ref(void* block);

/**
 * @brief Dencrement the reference count of a memory block
 *
 * @note It is recommended that you always use the MEM_UNREF macro which calls this function with the debug
 * information filled in.
 *
 * @note If block_ptr already points to a NULL pointer, this function will assume that the block was already freed and
 * return SUCCESS.
 *
 * @param[in,out] block_ptr A pointer to the memory block pointer to be dereferenced. If the reference count reaches 0,
 * the block pointer will be set to NULL.
 *
 * @return SUCCESS when the block's reference count was decremented.
 * @return -EINVAL when the provided pointer was not a valid memory block
 */
int mem_unref(void** block_ptr);

/**
 * @brief Convenience macro for @ref mem_alloc with memory tracing
 */
#define MEM_ALLOC(size, data) TRACE_WRAP(mem_alloc(size, data))

/**
 * @brief Convenience macro for @ref mem_ref with memory tracing
 */
#define MEM_REF(data) TRACE_WRAP(mem_ref(data))

/**
 * @brief Convenience macro for @ref mem_unref with memory tracing
 */
#define MEM_UNREF(data) TRACE_WRAP(mem_unref(data))

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

#endif  // MEMORY_H
