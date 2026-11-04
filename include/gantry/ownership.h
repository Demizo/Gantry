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
 * @brief Marks a point where ownership of a resource leaves the function
 *
 * @details Tells the resource checker that the caller (or another owner) is now responsible for releasing the value, so
 * it is not reported as leaked on this path. It releases the identifier whatever resource class it was acquired as.
 *
 * @releases * data
 */
#define PASS_OWNERSHIP(data)
