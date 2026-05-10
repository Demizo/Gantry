/**
 * @file
 * @brief Describe interface for the datastore
 *
 * @author Demizo (demizodemazo@gmail.com)
 * @date 2026-03-01
 *
 *
 */

#ifndef DATASTORE_DESCRIBE_H
#define DATASTORE_DESCRIBE_H

#include <stddef.h>
#include <zcbor_common.h>
#include <zephyr/kernel.h>

/**
 * @addtogroup datastore
 * @{
 */

//**********************************************************
//* Definitions
//**********************************************************

//**********************************************************
//* Typedefs, Enums, and Structs
//**********************************************************

/**
 * @brief State context for chunked datastore reading
 */
struct datastore_describe_state
{
    uint32_t current_id; /**< The ID of the next item to encode */
};

//**********************************************************
//* Functions
//**********************************************************

/**
 * @brief Initialize a datastore describe
 *
 * @details Call before starting describe transactions. After starting a describe, obtain the datastore description by
 * repeatedly calling @ref datastore_describe until there is no more data.
 *
 * @param[out] describe_state Describe state to initialize
 */
void datastore_describe_start(struct datastore_describe_state* describe_state);

/**
 * @brief Encode the next chunk of the datastore description
 *
 * @details The encoder's buffer will be filled with the datastore description until the description ends or there is no
 * more room in the buffer. describe_state tracks the position across subsequent calls. Call @ref
 * datastore_describe_start to reset the describe_state.
 *
 * @param describe_state State variable to track which items have already been described
 * @param encoder CBOR encoder to populate with a chunk of the description
 *
 * @return SUCCESS when the description is complete
 * @return -ENOMEM when there is no more room in the encoder
 */
int datastore_describe(struct datastore_describe_state* describe_state, zcbor_state_t* encoder);

/**
 * @}
 */

#endif  // DATASTORE_DESCRIBE_H
