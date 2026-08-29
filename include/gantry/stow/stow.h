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

#pragma once

#include <gantry/event.h>
#include <gantry/memory.h>
#include <gantry/stow/stow_event.h>
#include <gantry/stow/types/stow_types.h>
#include <generated_stow_items.h>
#include <stddef.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/iterable_sections.h>
#include <zephyr/sys/slist.h>
#include <zephyr/sys/util_macro.h>

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
 * @note Runs with interrupts masked, must be interrupt safe, any may not block.
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
    sys_slist_t subscribers;     /**< List of subscribers to the item */
    bool has_handle_subscribers; /**< Whether any subscriber uses @ref STOW_SUBSCRIPTION_HANDLE */
    bool has_copy_subscribers;   /**< Whether any subscriber uses @ref STOW_SUBSCRIPTION_COPY */
    bool dirty;                  /**< Whether the item has changed since its last save */
};

/**
 * @brief A static stow subscription
 *
 * @details Created by @ref STOW_SUBSCRIPTION_DEFINE. These subscriptions exist for the full lifetime of the app.
 */
struct stow_static_subscription
{
    enum stow_item_id id;                  /**< Item ID being subscribed to */
    struct stow_subscription subscription; /**< The stow subscription */
};

//**********************************************************
//* Functions
//**********************************************************

/**
 * @brief Initialize the stow
 *
 * @details Resets default values and subscriber state, then loads persistent stow item values.
 * Called automatically at boot before any application modules run.
 *
 * @note This function is public and idempotent so it can be used to reset state in testing.
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
 * @brief Write modified persistent items to storage
 *
 * @details Writes are normally flushed by a background job. Call this to persist
 * pending writes immediately, for example before a deliberate reset.
 *
 * @note Not interrupt safe.
 *
 * @return SUCCESS when every dirty item was saved
 * @return the first error encountered, having attempted every dirty item
 */
int stow_flush(void);

/**
 * @brief Set the value of a data item
 *
 * @note Safe to call from interrupt context. Persistence is deferred.
 *
 * @param id Item ID to modify
 * @param value The desired value
 *
 * @return SUCCESS when the value is set
 * @return -EACCES when the item is TOFU and has already been modified
 * @return -EINVAL when the provided value is invalid
 * @return -ENOMEM when the value cannot be stored
 */
int stow_set(enum stow_item_id id, data_value_t value);

/**
 * @brief Convenience macro for @ref stow_set with memory tracing
 */
#define STOW_SET(id, value) TRACE_WRAP(stow_set(id, value))

/**
 * @brief Set the value of a data item on behalf of an external client
 *
 * @details Checks @p current_auth against the item's write permissions before delegating to @ref stow_set.
 *
 * @param current_auth The caller's role bitmask
 * @param id Item ID to modify
 * @param value The desired value
 *
 * @return SUCCESS when the value is set
 * @return -EACCES when the caller's role has no overlap with the item's write permission mask, or the item is TOFU
 * and has already been modified
 * @return -EINVAL when the provided value is invalid
 * @return -ENOMEM when the value cannot be stored
 */
int stow_set_external(stow_role_t current_auth, enum stow_item_id id, data_value_t value);

/**
 * @brief Convenience macro for @ref stow_set_external with memory tracing
 */
#define STOW_SET_EXTERNAL(current_auth, id, value) TRACE_WRAP(stow_set_external(current_auth, id, value))

/**
 * @brief Get the current value of a data item
 *
 * @note Safe to call from interrupt context.
 *
 * @param[in] id Item ID to retrieve
 * @param[out] out_value Pointer to be populated with the item's current value
 *
 * @return SUCCESS when the value was retrieved
 * @return -EINVAL when the output pointer is NULL
 * @return -ENOMEM when the value cannot be retrieved
 */
int stow_get(enum stow_item_id id, data_value_t* out_value);

/**
 * @brief Convenience macro for @ref stow_get with memory tracing
 */
#define STOW_GET(id, out_value) TRACE_WRAP(stow_get(id, out_value))

/**
 * @brief Get the current value of a data item on behalf of an external client
 *
 * @details Checks @p current_auth against the item's read permissions before delegating to @ref stow_get.
 *
 * @param[in] current_auth The caller's role bitmask
 * @param[in] id Item ID to retrieve
 * @param[out] out_value Pointer to be populated with the item's current value
 *
 * @return SUCCESS when the value was retrieved
 * @return -EACCES when the caller's role has no overlap with the item's read permission mask
 * @return -EINVAL when the output pointer is NULL
 * @return -ENOMEM when the value cannot be retrieved
 */
int stow_get_external(stow_role_t current_auth, enum stow_item_id id, data_value_t* out_value);

/**
 * @brief Convenience macro for @ref stow_get_external with memory tracing
 */
#define STOW_GET_EXTERNAL(current_auth, id, out_value) TRACE_WRAP(stow_get_external(current_auth, id, out_value))

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
 * @note Safe to call from interrupt context.
 *
 * @param id Item ID to subscribe to
 * @param subscription The stow subscription
 *
 * @return SUCCESS when the subscription is added
 * @return -EALREADY when the requested subscription already exists
 * @return -ENOMEM when there is no memory to create a subscription
 */
int stow_subscribe(enum stow_item_id id, struct stow_subscription* subscription);

/**
 * @brief Subscribe to a data item on behalf of an external client
 *
 * @details Checks @p current_auth against the item's read permissions before delegating to @ref stow_subscribe.
 *
 * @param current_auth The caller's role bitmask
 * @param id Item ID to subscribe to
 * @param subscription The stow subscription
 *
 * @return SUCCESS when the subscription is added
 * @return -EACCES when the caller's role has no overlap with the item's read permission mask
 * @return -EALREADY when the requested subscription already exists
 * @return -ENOMEM when there is no memory to create a subscription
 */
int stow_subscribe_external(stow_role_t current_auth, enum stow_item_id id, struct stow_subscription* subscription);

/**
 * @brief Unsubscribe from a data item
 *
 * @note Safe to call from interrupt context.
 *
 * @param id Item ID to unsubscribe from
 * @param subscription The subscription to remove
 *
 * @return SUCCESS when the subscription is removed from the data item
 * @return -ENOENT when the subscription did not exist for the given data item
 */
int stow_unsubscribe(enum stow_item_id id, struct stow_subscription* subscription);

/**
 * @brief Statically define stow subscriptions
 *
 * @details Use this instead of a runtime @ref stow_subscribe call for subscriptions that live for the entire
 * process lifetime.
 *
 * @param _name Base name for subscription, must be unique within the translation unit
 * @param _mode Subscription mode, see @ref stow_subscription_mode
 * @param _cb Callback invoked when any listed item changes
 * @param ... One or more item IDs
 */
#define STOW_SUBSCRIPTION_DEFINE(_name, _mode, _cb, ...) \
    FOR_EACH_IDX_FIXED_ARG(Z_STOW_SUBSCRIPTION_DEFINE_ONE, (;), (_name, _mode, _cb), __VA_ARGS__)

/** @cond INTERNAL_HIDDEN */
#define Z_STOW_SUBSCRIPTION_APPLY(_macro, _args) _macro _args

#define Z_STOW_SUBSCRIPTION_DEFINE_ONE(_idx, _id, _fixed) \
    Z_STOW_SUBSCRIPTION_APPLY(Z_STOW_SUBSCRIPTION_DEFINE_ONE_, (_idx, _id, __DEBRACKET _fixed))

#define Z_STOW_SUBSCRIPTION_DEFINE_ONE_(_idx, _id, _name, _mode, _cb) \
    static const STRUCT_SECTION_ITERABLE(stow_static_subscription, _CONCAT(_name, _idx)) = {                         \
        .id = (_id),                                                                                                 \
        .subscription = {                                                                                            \
            .mode = (_mode),                                                                                         \
            .cb = (_cb),                                                                                             \
        },                                                                                                           \
    }
/** @endcond */

/**
 * @}
 */
