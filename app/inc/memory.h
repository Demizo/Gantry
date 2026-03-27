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
#define _ALLOC_TRACE , const char *func, const char *file, int line
#define _ALLOC_TRACE_INPUT , __func__, __FILE__, __LINE__
#define _ALLOC_TRACE_PASSTHROUGH , func, file, line

#define _REF_TRACE , const char *file, int line
#define _REF_TRACE_INPUT , __FILE__, __LINE__
#define _REF_TRACE_PASSTHROUGH , file, line
#else
#define _ALLOC_TRACE
#define _ALLOC_TRACE_INPUT
#define _ALLOC_TRACE_PASSTHROUGH

#define _REF_TRACE
#define _REF_TRACE_INPUT
#define _REF_TRACE_PASSTHROUGH
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
 * @param[in] _ALLOC_TRACE Trace information used for debugging
 *
 * @return SUCCESS when the allocation is successful.
 * @return -EINVAL the provided pointer was NULL, a size of 0 was requested, or the requested size was larger than the
 * maximum block size.
 * @return -ENOTEMPTY the block pointer was not pointing to a NULL pointer.
 * @return -ENOMEM No blocks were available that could fit the requested size.
 */
int mem_alloc(size_t size, void** block_ptr _ALLOC_TRACE);

/**
 * @brief Increment the reference count of a memory block
 *
 * @note It is recommended that you always use the MEM_REF macro which calls this function with the debug
 * information filled in.
 *
 * @param[in] block The memory block pointer to be reference counted
 * @param[in] _REF_TRACE Trace information used for debugging
 *
 * @return SUCCESS when the block's reference count was incremented
 * @return -EINVAL when the provided pointer was not a valid memory block
 * @return -ENOMEM when the block already has the maximum number of references
 */
int mem_ref(void* block _REF_TRACE);

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
 * @param[in] _REF_TRACE Trace information used for debugging
 *
 * @return SUCCESS when the block's reference count was decremented.
 * @return -EINVAL when the provided pointer was not a valid memory block
 */
int mem_unref(void** block_ptr _REF_TRACE);

/**
 * @brief Convenience macro for memory block allocation with automatic function/file/line tracking
 */
#define MEM_ALLOC(size, data) mem_alloc(size, data _ALLOC_TRACE_INPUT)

/**
 * @brief Convenience macro for memory block referencing with automatic file/line tracking
 */
#define MEM_REF(data) mem_ref(data _REF_TRACE_INPUT)

/**
 * @brief Convenience macro for memory block unreferencing with automatic file/line tracking
 */
#define MEM_UNREF(data) mem_unref(data _REF_TRACE_INPUT)

/**
 * @brief Indicates to static analysis that ownership over the memory will be the responsibility of the caller
 */
#define PASS_OWNERSHIP(data)

/**
 * @}
 */

#endif  // MEMORY_H
