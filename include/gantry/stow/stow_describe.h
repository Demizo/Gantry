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

#pragma once

#include <stddef.h>
#include <zcbor_common.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>

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
 * @details Fills the encoder with whole item descriptions, a CBOR sequence of item maps, from the first described item
 * at or after @p start_id. @p next_id_out is the next start ID, or the item count once every item is encoded.
 *
 * @param start_id    ID of the first item to encode (pass 0 to start from the beginning)
 * @param filter      Bitmap of the item IDs to describe, or NULL to describe every item
 * @param encoder     CBOR encoder to populate
 * @param next_id_out Populated with the ID of the next item to encode
 *
 * @return SUCCESS when all items starting from @p start_id were encoded
 * @return -ENOMEM when there is no more room in the encoder before all items were encoded
 * @return -E2BIG when the next item does not fit in the encoder at all
 */
int stow_describe(uint32_t start_id, const atomic_t* filter, zcbor_state_t* encoder, uint32_t* next_id_out);

/**
 * @}
 */
