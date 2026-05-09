/**
 * @file datastore_event.h
 *
 * @brief Datastore update event definition
 *
 * @author Demizo (demizodemazo@gmail.com)
 * @version 0.1
 * @date 2026-03-01
 *
 * @copyright Copyright (c) 2026
 *
 */

#ifndef DATASTORE_EVENT_H
#define DATASTORE_EVENT_H

#include <stddef.h>
#include <zds/datastore/types/datastore_types.h>
#include <zds/event.h>
#include <zephyr/kernel.h>

/**
 * @addtogroup datastore
 * @{
 */

//**********************************************************
//* Definitions
//**********************************************************

/**
 * @brief Event ID for datastore update events
 */
#define EVENT_ID_DATASTORE_UPDATE 1
/**
 * @brief Datastore update event declaration
 */
DECLARE_EVENT_TYPE(datastore_update_event);

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
    const struct datastore_item_const_metadata* metadata; /**< Constant metadata of the updated item */
    enum datastore_subscription_mode mode;                /**< Subscription mode */
    data_value_t value_copy; /**< A copy of the item's value, valid when the mode is @ref DATASTORE_SUBSCRIPTION_COPY */
};

//**********************************************************
//* Functions
//**********************************************************

/**
 * @brief Release the data copy within a @ref datastore_update_event_payload
 *
 * @param event Event being freed
 */
void datastore_event_on_free(event_t* event);

/**
 * @}
 */

#endif  // DATASTORE_EVENT_H
