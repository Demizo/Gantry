/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Self-registering application modules
 *
 * @author Demizo (demizodemazo@gmail.com)
 */

#include <gantry/error.h>
#include <gantry/module.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

/**
 * @brief Logger for module
 */
LOG_MODULE_REGISTER(gantry_module, CONFIG_GANTRY_MODULE_LOG_LEVEL);

//**********************************************************
//* Static Function Definitions
//**********************************************************

/**
 * @brief Start every module registered via @ref GANTRY_MODULE_DEFINE or
 * @ref GANTRY_LIBRARY_MODULE_DEFINE
 *
 * @details Registered as a `SYS_INIT` entry at `APPLICATION` level. This runs automatically on startup after the Stow
 * has been initialized.
 *
 * @return SUCCESS always, to satisfy the `SYS_INIT` signature
 */
static int gantry_modules_start(void)
{
    STRUCT_SECTION_FOREACH(gantry_module, module)
    {
        module->init();

        if (module->thread_entry != NULL)
        {
            k_thread_create(
                module->thread_data, module->stack, module->stack_size, module->thread_entry, NULL, NULL, NULL,
                module->priority, 0, K_NO_WAIT);
            k_thread_name_set(module->thread_data, module->name);
        }

        LOG_INF("Started module \"%s\"", module->name);
    }

    return SUCCESS;
}

/**
 * @brief Initialization entry for application modules
 */
SYS_INIT(gantry_modules_start, APPLICATION, CONFIG_GANTRY_MODULE_INIT_PRIORITY);
