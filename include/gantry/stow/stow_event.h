/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Stow update event definition
 *
 * @author Demizo (demizodemazo@gmail.com)
 *
 *
 */

#ifndef STOW_EVENT_H
#define STOW_EVENT_H

#include <gantry/event.h>
#include <gantry/stow/types/stow_types.h>
#include <stddef.h>
#include <zephyr/kernel.h>

/**
 * @addtogroup stow
 * @{
 */

//**********************************************************
//* Definitions
//**********************************************************

/**
 * @brief Event ID for stow update events
 */
#define EVENT_ID_STOW_UPDATE 1
/**
 * @brief Stow update event declaration
 */
DECLARE_EVENT_TYPE(stow_update_event);

//**********************************************************
//* Typedefs, Enums, and Structs
//**********************************************************

/**
 * @brief Subscription mode for stow item subscriptions
 */
enum stow_subscription_mode
{
    STOW_SUBSCRIPTION_HANDLE, /**< Subscribers are notified of which value has been updated, the subscriber must
                                      read the current value from the stow. */
    STOW_SUBSCRIPTION_COPY,   /**< Used for guaranteed delivery of every value. The notification contains a copy of
                                      the value at the time when the notification occurred. */
    STOW_SUBSCRIPTION_COUNT
};

/**
 * @brief Event payload for stow update events
 */
struct stow_update_event_payload
{
    const struct stow_item_const_metadata* metadata; /**< Constant metadata of the updated item */
    enum stow_subscription_mode mode;                /**< Subscription mode */
    data_value_t value_copy; /**< A copy of the item's value, valid when the mode is @ref STOW_SUBSCRIPTION_COPY */
};

//**********************************************************
//* Functions
//**********************************************************

/**
 * @brief Release the data copy within a @ref stow_update_event_payload
 *
 * @param event Event being freed
 */
void stow_event_on_free(event_t* event);

/**
 * @}
 */

#endif  // STOW_EVENT_H
