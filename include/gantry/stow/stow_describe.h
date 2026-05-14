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

//**********************************************************
//* Functions
//**********************************************************

/**
 * @brief Encode a chunk of the stow description starting at a given item ID
 *
 * @details The encoder's buffer will be filled with item descriptions starting
 * from @p start_id. Encoding stops when all items are encoded or the buffer is
 * full. On return, @p next_id_out holds the ID to pass as @p start_id on the
 * next call. When @p next_id_out equals the total item count, all items have
 * been encoded.
 *
 * @param start_id    ID of the first item to encode (pass 0 to start from the beginning)
 * @param encoder     CBOR encoder to populate
 * @param next_id_out Populated with the ID of the next item to encode
 *
 * @return SUCCESS when all items starting from @p start_id were encoded
 * @return -ENOMEM when there is no more room in the encoder before all items were encoded
 */
int stow_describe(uint32_t start_id, zcbor_state_t* encoder, uint32_t* next_id_out);

/**
 * @}
 */

#endif  // STOW_DESCRIBE_H
