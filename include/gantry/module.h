/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Composable application modules
 *
 * @author Demizo (demizodemazo@gmail.com)
 */

#pragma once

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

/**
 * @defgroup gantry_module Module
 * @{
 */

/**
 * @brief Module init callback
 *
 * @details Called once at boot, after Stow is initialized and before the module's thread (if
 * any) is created.
 */
typedef void (*gantry_module_init_fn)(void);

/**
 * @brief An application module registration
 *
 * @details Created by @ref GANTRY_MODULE_DEFINE or @ref GANTRY_LIBRARY_MODULE_DEFINE.
 */
struct gantry_module
{
    const char* name;              /**< Thread name */
    gantry_module_init_fn init;    /**< Called once before the thread (if any) is created */
    k_thread_entry_t thread_entry; /**< Thread entry point, or NULL for a library module */
    k_thread_stack_t* stack;       /**< Thread stack, unused for a library module */
    size_t stack_size;             /**< Thread stack size in bytes, unused for a library module */
    struct k_thread* thread_data;  /**< Thread control block, unused for a library module */
    int priority;                  /**< Thread priority, unused for a library module */
};

/** @cond INTERNAL_HIDDEN */
#define Z_GANTRY_MODULE_DEFINE(_name, _init, _thread_entry, _stack, _stack_size, _thread_data, _priority) \
    static const STRUCT_SECTION_ITERABLE(gantry_module, _name) = {                                        \
        .name = STRINGIFY(_name),                                                                         \
        .init = (_init),                                                                                  \
        .thread_entry = (_thread_entry),                                                                  \
        .stack = (_stack),                                                                                \
        .stack_size = (_stack_size),                                                                      \
        .thread_data = (_thread_data),                                                                    \
        .priority = (_priority),                                                                          \
    }
/** @endcond */

/**
 * @brief Register an application module
 *
 * @details Declares and application module with its own thread. Modules are started automatically at boot: @p _init is
 * called first, then a thread running @p _thread_entry is created.
 *
 * @note Modules run after the Stow is initialized. The initialization order between application modules in not
 * guaranteed.
 *
 * @param _name Module name
 * @param _init Init function, called before the thread is created
 * @param _thread_entry Thread entry point
 * @param _stack_size Thread stack size in bytes
 * @param _priority Thread priority
 */
#define GANTRY_MODULE_DEFINE(_name, _init, _thread_entry, _stack_size, _priority)                           \
    K_THREAD_STACK_DEFINE(_CONCAT(_name, _stack), (_stack_size));                                           \
    static struct k_thread _CONCAT(_name, _thread_data);                                                    \
    Z_GANTRY_MODULE_DEFINE(                                                                                 \
        _name, _init, _thread_entry, _CONCAT(_name, _stack), K_THREAD_STACK_SIZEOF(_CONCAT(_name, _stack)), \
        &_CONCAT(_name, _thread_data), (_priority))

/**
 * @brief Register a library module
 *
 * @details Declares a library module, a module without a dedicated thread.
 *
 * @note Modules run after the Stow is initialized. The initialization order between application modules in not
 * guaranteed.
 *
 * @param _name Module name
 * @param _init Init function
 */
#define GANTRY_LIBRARY_MODULE_DEFINE(_name, _init) Z_GANTRY_MODULE_DEFINE(_name, _init, NULL, NULL, 0, NULL, 0)

/**
 * @}
 */
