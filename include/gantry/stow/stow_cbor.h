/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Definite-length CBOR container headers for Stow encoders
 *
 * @details Stow encodes every array and map with its element count up front, so a container needs no end marker and
 * nothing is moved after encoding.
 *
 * @author Demizo (demizodemazo@gmail.com)
 *
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <zcbor_common.h>
#include <zcbor_encode.h>

/**
 * @addtogroup stow
 * @{
 */

//**********************************************************
//* Functions
//**********************************************************

/**
 * @brief Encode a definite-length container header
 *
 * @param encoder CBOR encoder
 * @param major_type Major type of the container
 * @param count Number of elements, or of key-value pairs for a map
 *
 * @return true when the header was encoded
 */
static inline bool stow_cbor_container_start(zcbor_state_t* encoder, zcbor_major_type_t major_type, uint32_t count)
{
    uint8_t* header = encoder->payload_mut;
    if (!zcbor_uint32_put(encoder, count))
    {
        return false;
    }
    *header |= (uint8_t)((uint8_t)major_type << 5);
    return true;
}

/**
 * @brief Encode the header of a definite-length array
 *
 * @param encoder CBOR encoder
 * @param count Number of elements that follow
 *
 * @return true when the header was encoded
 */
static inline bool stow_cbor_list_start(zcbor_state_t* encoder, uint32_t count)
{
    return stow_cbor_container_start(encoder, ZCBOR_MAJOR_TYPE_LIST, count);
}

/**
 * @brief Encode the header of a definite-length map
 *
 * @param encoder CBOR encoder
 * @param count Number of key-value pairs that follow
 *
 * @return true when the header was encoded
 */
static inline bool stow_cbor_map_start(zcbor_state_t* encoder, uint32_t count)
{
    return stow_cbor_container_start(encoder, ZCBOR_MAJOR_TYPE_MAP, count);
}

/**
 * @}
 */
