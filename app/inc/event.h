/**
 * @file event.h
 *
 * @brief Events for interprocess communication
 *
 * @details Communication between threads is accomplished by passing events. There is a single event structure with a
 * type field to indicate event type. This ensures that all modules can communicate without needing to agree on
 * arbitrary event structures. Applications can define event types using @ref DEFINE_EVENT_TYPE.
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

/**
 * @brief Declare a custom @ref event_type_t to be used by other modules. The event must be defined using @ref
 * DEFINE_EVENT_TYPE in the corresponding source file.
 */
#define DECLARE_EVENT_TYPE(_name) extern const event_type_t _name

/**
 * @brief Define an @ref event_type_t. Event IDs will automatically be checked for collisions.
 */
#define DEFINE_EVENT_TYPE(_id, _name, _on_free)              \
    const uint8_t CONCAT(__event_collision_check_, _id) = 0; \
    const event_type_t _name = {                             \
        .id = _id,                                           \
        .on_free = _on_free,                                 \
    };

//**********************************************************
//* Typedefs, Enums, and Structs
//**********************************************************

/**
 * @brief Forward declaration of universal event structure
 */
typedef struct event_t event_t;

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
 * @brief Function called before an event is freed
 *
 * @param event The event being freed
 */
typedef void (*event_on_free_t)(event_t* event);

/**
 * @brief Event type
 *
 * @details Applications can define event types using @ref DEFINE_EVENT_TYPE. Event types may contain reference counted
 * memory in the payload. In this case @ref on_free should be defined to dereference the event payload when the event is
 * freed.
 */
typedef struct
{
    uint32_t id;             /**< Numeric ID representing the event type */
    event_on_free_t on_free; /**< Optional function to be called before the event is freed, can be NULL */
} event_type_t;

/**
 * @brief Universal event structure
 */
struct event_t
{
    event_t* next_event;         /**< Linked event */
    struct k_msgq* return_queue; /**< Optional return queue for responses */
    event_type_t* type;          /**< Event type */
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
 * @param[in] type The type of the event
 * @param[out] event_ptr Pointer to be populated with the address of an event. This pointer must point to a NULL
 * pointer when this function is called. It is only populated when the allocation succeeds.
 *
 * @return result of @ref mem_alloc
 */
int event_alloc(size_t size, event_direction_t direction, event_type_t* type, event_t** event_ptr);

/**
 * @brief Convenience macro for @ref event_alloc with memory tracing
 */
#define EVENT_ALLOC(size, direction, format, event) TRACE_WRAP(event_alloc(size, direction, format, event))

/**
 * @brief Increment the reference count of an event and all linked events
 *
 * @note It is recommended that you always use the EVENT_REF macro which calls this function with the debug
 * information filled in.
 *
 * @param[in] event The event pointer to be reference counted
 */
void event_ref(event_t* event);

/**
 * @brief Convenience macro for @ref event_ref with memory tracing
 */
#define EVENT_REF(event) TRACE_WRAP_VOID(event_ref(event))

/**
 * @brief Dencrement the reference count of an event and all linked events
 *
 * @note It is recommended that you always use the EVENT_UNREF macro which calls this function with the debug
 * information filled in.
 *
 * @note If event_ptr already points to a NULL pointer, this function will assume that the event was already freed and
 * do nothing.
 *
 * @param[in,out] event_ptr A pointer to the event pointer to be dereferenced. If the reference count reaches 0,
 * the event pointer will be set to NULL.
 */
void event_unref(event_t** event_ptr);

/**
 * @brief Convenience macro for @ref event_unref with memory tracing
 */
#define EVENT_UNREF(event) TRACE_WRAP_VOID(event_unref(event))

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
 * @param[in] type The type of the event
 */
void event_init(event_t* event, size_t size, event_direction_t direction, event_type_t* type);

/**
 * @}
 */

#endif  // EVENT_H
