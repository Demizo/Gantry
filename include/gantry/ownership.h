/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Marks where the resource checker stops tracking a resource
 */

#pragma once

/**
 * @brief Marks a point where the function's obligation for a resource ends without a tagged release
 *
 * @details Tells the resource checker that the value is not leaked on this path: another owner, such as the caller or
 * a queue, is now responsible for releasing it, or the function implements the resource's class and has released it
 * itself. It releases the identifier whatever resource class it was acquired as.
 *
 * @releases * data
 */
#define PASS_OWNERSHIP(data)
