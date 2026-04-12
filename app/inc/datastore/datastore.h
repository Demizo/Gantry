/**
 * @file datastore.h
 *
 * @brief Core internal interface for the datastore
 *
 * @author Demizo (demizodemazo@gmail.com)
 * @version 0.1
 * @date 2026-03-01
 *
 * @copyright Copyright (c) 2026
 *
 */

#ifndef DATASTORE_H
#define DATASTORE_H

#include <stddef.h>
#include <zephyr/kernel.h>

#include "datastore_event.h"
#include "datastore_types.h"
#include "event.h"
#include "generated_datastore_items.h"
#include "memory.h"
#include "zephyr/sys/slist.h"

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
 * @brief Callback used to notify a subscriber when the subscribed value changes
 *
 * @param event Pointer to a datastore update event
 */
typedef void (*datastore_subscription_cb)(event_t* event);

/**
 * @brief Datastore subscription
 *
 * @details Defines the subscription mode and callback to be called when the item is updated.
 */
struct datastore_subscription
{
    enum datastore_subscription_mode mode; /**< Subscription mode */
    datastore_subscription_cb cb;          /**< Callback */
};

/**
 * @brief Dynamic metadata for datastore items
 */
struct datastore_item_dynamic_metadata
{
    sys_slist_t subscribers; /**< List of subscribers to the item */
};

//**********************************************************
//* Functions
//**********************************************************

/**
 * @brief Initialize the datastore
 *
 * Loads persistent datastore item values. Must be called before the datastore is used.
 */
void datastore_init(void);

/**
 * @brief Check if a numeric ID is a valid datastore item ID
 *
 * @param id the numeric ID to check
 * @return true If the ID can be cast to a @ref datastore_item_id
 * @return false If the ID is not a valid item ID
 */
bool datastore_is_id_valid(uint32_t id);

/**
 * @brief Set the value of a data item
 *
 * @param current_auth The current authentication level
 * @param id Item ID to modify
 * @param value The desired value
 *
 * @return SUCCESS when the value is set
 * @return -EACCES when the current authentication level is not sufficient
 * @return -EINVAL when the provided value is invalid
 * @return -ENOMEM when the value cannot be stored
 */
int datastore_set(enum datastore_auth_level current_auth, enum datastore_item_id id, data_value_t value);

/**
 * @brief Convenience macro for @ref datastore_set with memory tracing
 */
#define DATASTORE_SET(current_auth, id, value) TRACE_WRAP(datastore_set(current_auth, id, value))

/**
 * @brief Get the current value of a data item
 *
 * @param[in] current_auth The current authentication level
 * @param[in] id Item ID to retrieve
 * @param[out] out_value Pointer to be populated with the item's current value
 *
 * @return SUCCESS when the value was retrieved
 * @return -EACCES when the current authentication level is not sufficient
 * @return -EINVAL when the output pointer is NULL
 * @return -ENOMEM when the value cannot be retrieved
 */
int datastore_get(enum datastore_auth_level current_auth, enum datastore_item_id id, data_value_t* out_value);

/**
 * @brief Convenience macro for @ref datastore_get with memory tracing
 */
#define DATASTORE_GET(current_auth, id, out_value) TRACE_WRAP(datastore_get(current_auth, id, out_value))

/**
 * @brief Release a previously retrieved data item value
 *
 * @details @ref datastore_get may return a pointer to a memory block depending on the item's data type. As a result,
 * item values retrieved with @ref datastore_get should always be released.
 *
 * @param[in] id Item ID, used to determine the data type
 * @param[in,out] value Pointer to the value to release.
 *
 * @return SUCCESS when the value was released
 * @return -EINVAL when the value could not be released
 */
int datastore_release(enum datastore_item_id id, data_value_t* value);

/**
 * @brief Convenience macro for @ref datastore_release with debug tracing
 */
#define DATASTORE_RELEASE(id, value) TRACE_WRAP(datastore_release(id, value))

/**
 * @brief Encode a data item value as CBOR
 *
 * @param encoder CBOR encoder instance
 * @param id Item ID, used to determine the data type
 * @param value The value to encode
 *
 * @return SUCCESS when the value was encoded
 * @return -ENOMEM when the encoder lacks room to encode the value
 */
int datastore_encode(zcbor_state_t* encoder, enum datastore_item_id id, data_value_t value);

/**
 * @brief Decode a data item value from CBOR
 *
 * @param[in] decoder CBOR decoder instance
 * @param[in] id Item ID, used to determine the data type
 * @param[out] out_value Pointer populated with the decoded value
 *
 * @return SUCCESS when the value was decoded
 * @return -EBADMSG when the CBOR value could not be decoded
 * @return -ENOMEM when memory cannot be allocated for the decoded value
 */
int datastore_decode(zcbor_state_t* decoder, enum datastore_item_id id, data_value_t* out_value);

/**
 * @brief Convenience macro for @ref datastore_decode with debug tracing
 */
#define DATASTORE_DECODE(decoder, id, out_value) TRACE_WRAP(datastore_decode(decoder, id, out_value))

/**
 * @}
 */

#endif  // DATASTORE_H
