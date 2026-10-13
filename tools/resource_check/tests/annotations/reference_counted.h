/*
 * Annotations for the reference-counted allocator, event and value APIs that the unit tests and fixtures call. This
 * file is only scanned, never compiled or included.
 */

/** @acquires memory block_ptr on-success */
int mem_alloc(size_t size, void** block_ptr);

/** @acquires memory data on-success */
#define MEM_ALLOC(size, data) mem_alloc(size, data)

/** @acquires memory block */
void mem_ref(void* block);

/** @acquires memory data */
#define MEM_REF(data) mem_ref(data)

/** @releases memory block_ptr */
void mem_unref(void** block_ptr);

/** @releases memory data */
#define MEM_UNREF(data) mem_unref(data)

/** @releases * data */
#define PASS_OWNERSHIP(data)

/** @acquires memory event_ptr on-success */
int event_alloc(const event_type_t* type, size_t payload_size, event_t** event_ptr);

/** @acquires memory event_ptr on-success */
#define EVENT_ALLOC(type, payload_size, event_ptr) event_alloc(type, payload_size, event_ptr)

/** @releases memory event_ptr */
void event_unref(event_t** event_ptr);

/** @releases memory event */
#define EVENT_UNREF(event) event_unref(event)

/** @acquires memory out_value on-success */
int stow_get(enum stow_item_id id, data_value_t* out_value);

/** @acquires memory out_value on-success */
#define STOW_GET(id, out_value) stow_get(id, out_value)

/** @releases memory value */
void stow_release(enum stow_item_id id, data_value_t* value);

/** @releases memory value */
#define STOW_RELEASE(id, value) stow_release(id, value)
