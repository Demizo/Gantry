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

#include "datastore_types.h"
#include "event.h"
#include "generated_datastore_items.h"
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
 * @brief Subscription mode for datastore item subscriptions
 */
enum datastore_subscription_mode
{
    DATASTORE_SUBSCRIPTION_HANDLE, /**< Subscribers are notified of which value has been updated, the subscriber must
                                      read the current value from the datastore. */
    DATASTORE_SUBSCRIPTION_COPY,   /**< Used for guaranteed delivery of every value. The notification contains a copy of
                                      the value at the time when the notification occurred. */
    DATASTORE_SUBSCRIPTION_COUNT
};

/**
 * @brief Event payload for datastore update events
 */
struct datastore_update_event_payload
{
    struct datastore_item_const_metadata metadata; /**< Constant metadata of the updated item */
    enum datastore_subscription_mode mode;         /**< Subscription mode */
    const void* value_copy;                        /**< Pointer to a copy of the item's data, NULL when the
                                                mode is DATASTORE_SUBSCRIPTION_HANDLE */
};

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
 * @brief Set the value of a data item
 *
 * @param id Item ID to modify
 * @param value Pointer to the desired value
 *
 * @return SUCCESS when the value is set
 * @return -EINVAL when the provided value or item ID is invalid
 * @return -ENOMEM when the value cannot be stored
 */
int datastore_set(enum datastore_item_id id, const void* value);

/**
 * @brief Get the current value of a data item
 *
 * @param[in] id Item ID to retrieve
 * @param[out] out_value Pointer to be populated with the item's current value
 *
 * @return SUCCESS when the value was retrieved
 * @return -EINVAL when the provided item ID is invalid or the output pointer is NULL
 * @return -ENOMEM when the value cannot be retrieved
 */
int datastore_get(enum datastore_item_id id, void** out_value);

/**
 * @brief Release a previously retrieved data item value
 *
 * @details @ref datastore_get may return a pointer to a memory block depending on the item's data type. As a result,
 * item values retrieved with @ref datastore_get should always be released.
 *
 * @param[in] id Item ID to release
 * @param[in,out] value Pointer to the value to release. This will be set to NULL after release.
 *
 * @return SUCCESS when the value was released
 * @return -EINVAL when the value could not be released
 */
int datastore_release(enum datastore_item_id id, void** value);

/**
 * @}
 */

#endif  // DATASTORE_H
