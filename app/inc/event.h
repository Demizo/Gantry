/**
 * @file event.h
 *
 * @brief Events for interprocess communication
 *
 * @details Communication between threads is accomplished by passing events. There is a single event structure with a
 * format field to indicate event type. This ensures that all modules can communicate without needing to agree on
 * arbitrary event structures.
 *
 * Events are pointers to reference counted memory blocks. Modules are responsible for
 * dereferencing events when finished. Modules may pass events to other modules via function calls, but it is the
 * receiving module's responsibility to reference count the event if it wishes to retain it.
 *
 * Events may be linked to other events. When an event is referenced or dereferenced all events in the chain have their
 * reference counts updated.
 *
 * @author Demizo (demizodemazo@gmail.com)
 * @version 0.1
 * @date 2026-03-01
 *
 * @copyright Copyright (c) 2026
 *
 */

#ifndef EVENT_H
#define EVENT_H

#include <stddef.h>
#include <zephyr/kernel.h>

#include "buffer.h"
#include "memory.h"

/**
 * @addtogroup data_management
 * @{
 */

//**********************************************************
//* Definitions
//**********************************************************

//**********************************************************
//* Typedefs, Enums, and Structs
//**********************************************************

/**
 * @brief Event direction
 */
typedef enum
{
    EVENT_DIRECTION_RX = 0, /**< Receive event */
    EVENT_DIRECTION_TX = 1, /**< Transmit event */
    EVENT_DIRECTION_IX = 2, /**< Internal event */
} event_direction_t;

/**
 * @brief Event format
 */
typedef enum
{
    EVENT_FORMAT_BYTES = 0,            /**< Raw byte array */
    EVENT_FORMAT_DATASTORE_UPDATE = 1, /**< Datastore update */
    EVENT_FORMAT_MESSAGE = 3,          /**< Protocol message */
} event_format_t;

/**
 * @brief Forward declaration of universal event structure
 */
typedef struct event_t event_t;

/**
 * @brief Universal event structure
 */
struct event_t
{
    event_t* next_event;         /**< Linked event */
    struct k_msgq* return_queue; /**< Optional return queue for responses */
    event_format_t format;       /**< Event format */
    event_direction_t direction; /**< Event direction */
    buffer_t data;               /**< Event data buffer */
};

//**********************************************************
//* Functions
//**********************************************************

/**
 * @brief Allocate an event
 *
 * @details An event will be allocated based on the requested size.
 *
 * @note It is recommended that you always use the EVENT_ALLOC macro which calls this function with the debug
 * information filled in.
 *
 * @param[in] size The requested event buffer size in bytes
 * @param[in] direction The direction of the event
 * @param[in] format The format of the event
 * @param[out] event_ptr Pointer to be populated with the address of an event. This pointer must point to a NULL
 * pointer when this function is called. It is only populated when the allocation succeeds.
 * @param[in] _ALLOC_TRACE Trace information used for debugging
 *
 * @return result of @ref mem_alloc
 */
int event_alloc(size_t size, event_direction_t direction, event_format_t format, event_t** event_ptr _ALLOC_TRACE);

/**
 * @brief Increment the reference count of an event and all linked events
 *
 * @note It is recommended that you always use the EVENT_REF macro which calls this function with the debug
 * information filled in.
 *
 * @param[in] event The event pointer to be reference counted
 * @param[in] _REF_TRACE Trace information used for debugging
 *
 * @return result of @ref mem_ref
 */
int event_ref(event_t* event _REF_TRACE);

/**
 * @brief Dencrement the reference count of an event and all linked events
 *
 * @note It is recommended that you always use the EVENT_UNREF macro which calls this function with the debug
 * information filled in.
 *
 * @note If event_ptr already points to a NULL pointer, this function will assume that the event was already freed and
 * return SUCCESS.
 *
 * @param[in,out] event_ptr A pointer to the event pointer to be dereferenced. If the reference count reaches 0,
 * the event pointer will be set to NULL.
 * @param[in] _REF_TRACE Trace information used for debugging
 *
 * @return result of @ref mem_unref
 */
int event_unref(event_t** event_ptr _REF_TRACE);

/**
 * @brief Initialize an event structure
 *
 * @details If there is a need to initialize an event in a pre-existing memory block, this helper can be used.
 *
 * @note The size is only the length of the event data buffer. When initializing in
 * a memory block ensure the size needed to hold the event header is taken into account.
 *
 * @param[in,out] event Pointer to the event structure to initialize
 * @param[in] size Size of the event buffer
 * @param[in] direction The direction of the event
 * @param[in] format The format of the event
 */
void event_init(event_t* event, size_t size, event_direction_t direction, event_format_t format);

/**
 * @brief Convenience macro for event allocation with automatic file/line
 * tracking
 */
#define EVENT_ALLOC(size, direction, format, event) event_alloc(size, direction, format, event _ALLOC_TRACE_INPUT)

/**
 * @brief Convenience macro for event referencing with automatic file/line
 * tracking
 */
#define EVENT_REF(event) event_ref(event _REF_TRACE_INPUT)

/**
 * @brief Convenience macro for event unreferencing with automatic file/line
 * tracking
 */
#define EVENT_UNREF(event) event_unref(event _REF_TRACE_INPUT)

/**
 * @}
 */

#endif  // EVENT_H
