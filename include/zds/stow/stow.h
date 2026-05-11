/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Core internal interface for the stow
 *
 * @author Demizo (demizodemazo@gmail.com)
 *
 *
 */

#ifndef STOW_H
#define STOW_H

#include <generated_stow_items.h>
#include <stddef.h>
#include <zds/event.h>
#include <zds/memory.h>
#include <zds/stow/stow_event.h>
#include <zds/stow/types/stow_types.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/slist.h>

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
 * @brief Callback used to notify a subscriber when the subscribed value changes
 *
 * @param event Pointer to a stow update event
 */
typedef void (*stow_subscription_cb)(event_t* event);

/**
 * @brief Stow subscription
 *
 * @details Defines the subscription mode and callback to be called when the item is updated.
 */
struct stow_subscription
{
    enum stow_subscription_mode mode; /**< Subscription mode */
    stow_subscription_cb cb;          /**< Callback */
};

/**
 * @brief Dynamic metadata for stow items
 */
struct stow_item_dynamic_metadata
{
    sys_slist_t subscribers; /**< List of subscribers to the item */
};

//**********************************************************
//* Functions
//**********************************************************

/**
 * @brief Initialize the stow
 *
 * Loads persistent stow item values. Must be called before the stow is used.
 */
void stow_init(void);

/**
 * @brief Check if a numeric ID is a valid stow item ID
 *
 * @param id the numeric ID to check
 * @return true If the ID can be cast to a stow_item_id
 * @return false If the ID is not a valid item ID
 */
bool stow_is_id_valid(uint32_t id);

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
int stow_set(enum stow_auth_level current_auth, enum stow_item_id id, data_value_t value);

/**
 * @brief Convenience macro for @ref stow_set with memory tracing
 */
#define STOW_SET(current_auth, id, value) TRACE_WRAP(stow_set(current_auth, id, value))

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
int stow_get(enum stow_auth_level current_auth, enum stow_item_id id, data_value_t* out_value);

/**
 * @brief Convenience macro for @ref stow_get with memory tracing
 */
#define STOW_GET(current_auth, id, out_value) TRACE_WRAP(stow_get(current_auth, id, out_value))

/**
 * @brief Release a previously retrieved data item value
 *
 * @details @ref stow_get may return a pointer to a memory block depending on the item's data type. As a result,
 * item values retrieved with @ref stow_get should always be released.
 *
 * @param[in] id Item ID, used to determine the data type
 * @param[in,out] value Pointer to the value to release.
 */
void stow_release(enum stow_item_id id, data_value_t* value);

/**
 * @brief Convenience macro for @ref stow_release with debug tracing
 */
#define STOW_RELEASE(id, value) TRACE_WRAP_VOID(stow_release(id, value))

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
int stow_encode(zcbor_state_t* encoder, enum stow_item_id id, data_value_t value);

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
int stow_decode(zcbor_state_t* decoder, enum stow_item_id id, data_value_t* out_value);

/**
 * @brief Convenience macro for @ref stow_decode with debug tracing
 */
#define STOW_DECODE(decoder, id, out_value) TRACE_WRAP(stow_decode(decoder, id, out_value))

/**
 * @brief Subscribe to a data item
 *
 * @param current_auth The current authentication level
 * @param id Item ID to subscribe to
 * @param subscription The stow subscription
 *
 * @return SUCCESS when the subscription is added
 * @return -EACCES when the current authentication level is not sufficient
 * @return -EALREADY when the requested subscription already exists
 * @return -ENOMEM when there is no memory to create a subscription
 */
int stow_subscribe(enum stow_auth_level current_auth, enum stow_item_id id, struct stow_subscription* subscription);

/**
 * @brief Unsubscribe from a data item
 *
 * @param id Item ID to unsubscribe from
 * @param subscription The subscription to remove
 *
 * @return SUCCESS when the subscription is removed from the data item
 * @return -ENOENT when the subscription did not exist for the given data item
 */
int stow_unsubscribe(enum stow_item_id id, struct stow_subscription* subscription);

/**
 * @}
 */

#endif  // STOW_H
