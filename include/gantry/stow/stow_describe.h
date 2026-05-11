/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Describe interface for the stow
 *
 * @author Demizo (demizodemazo@gmail.com)
 *
 *
 */

#ifndef STOW_DESCRIBE_H
#define STOW_DESCRIBE_H

#include <stddef.h>
#include <zcbor_common.h>
#include <zephyr/kernel.h>

/**
 * @addtogroup stow
 * @{
 */

//**********************************************************
//* Definitions
//**********************************************************

//**********************************************************
//* Typedefs, Enums, and Structs
//**********************************************************

/**
 * @brief State context for chunked stow reading
 */
struct stow_describe_state
{
    uint32_t current_id; /**< The ID of the next item to encode */
};

//**********************************************************
//* Functions
//**********************************************************

/**
 * @brief Initialize a stow describe
 *
 * @details Call before starting describe transactions. After starting a describe, obtain the stow description by
 * repeatedly calling @ref stow_describe until there is no more data.
 *
 * @param[out] describe_state Describe state to initialize
 */
void stow_describe_start(struct stow_describe_state* describe_state);

/**
 * @brief Encode the next chunk of the stow description
 *
 * @details The encoder's buffer will be filled with the stow description until the description ends or there is no
 * more room in the buffer. describe_state tracks the position across subsequent calls. Call @ref
 * stow_describe_start to reset the describe_state.
 *
 * @param describe_state State variable to track which items have already been described
 * @param encoder CBOR encoder to populate with a chunk of the description
 *
 * @return SUCCESS when the description is complete
 * @return -ENOMEM when there is no more room in the encoder
 */
int stow_describe(struct stow_describe_state* describe_state, zcbor_state_t* encoder);

/**
 * @}
 */

#endif  // STOW_DESCRIBE_H
