========
Stow API
========

Overview
========

The Stow API allows firmware modules to set, get, and subscribe to items in the Stow. The API is generic meaning the same interface can be used regardless of an item's data type. Tagged unions are used to ensure type safety alongside runtime type checking. Some data types exist on the stack while others live in reference counted memory blocks. The generic interface abstracts this complexity from the caller. 

All Stow operations are validated against the given item's access controls and value constraints. It can be assumed that values in the Stow are always valid for a given item.

The Stow API is non-blocking, thread-safe, and can be called from interrupt contexts. The permanent storage of persistent items is deferred. A flush occurs ``CONFIG_STOW_STORAGE_FLUSH_DELAY_MS`` after a write. Call :any:`stow_flush` from a thread context to persist everything pending immediately.

Initialization
==============

Call :any:`stow_init` once at startup before accessing any items. This loads persistent values from flash.

Storage
-------

Changes to persistent values are stored using Zephyr's Settings module. The Zephyr Settings module supports various storage backends to choose from, custom backends can also be defined.

Validation
----------

When saved values are loaded from storage, they are validated against the item's *current* constraints. If the saved value is no longer valid, the item is reverted to its default and the saved value is discarded. For structs, all fields must be valid for the saved value to be valid. Notably, if the structure of a struct changes (e.g. fields are added or removed) old saved values are invalidated.

The Stow does not provide any complex migration mechanisms, but the automatic validation should cover most use cases. Ideally, usage of the Stow should lend itself to atomic data items that do not require complex migration. If migration is necessary, consider a custom implementation of ``stow_storage.c`` to intercept and migrate stored values.

Get Items
=========

Use :any:`STOW_GET` to read an item's current value. **Always** call :any:`STOW_RELEASE` when finished with the value retrieved by :any:`STOW_GET`. This allows types that are reference-counted to be released, it is a noop for other types. The :doc:`Resource Checker </resource_checker/index>` will verify that values are released.

.. code-block:: c

   data_value_t val = {0};
   STOW_GET(STOW_ID_DEVICE_NAME, &val);
   LOG_INF("Device name: %s", val.data.string_value);
   STOW_RELEASE(STOW_ID_DEVICE_NAME, &val);

.. important::

   The caller **MUST NOT** modify the value returned by :any:`STOW_GET` directly. If modifications are needed, make a copy of the value first.

.. mermaid::

   %%{init: {"theme": "neutral"}}%%
   sequenceDiagram
       participant Caller
       participant Stow

       Caller->>Stow: STOW_GET(item_id, &value)
       Stow-->>Caller: Populate value with a reference or copy
       Note over Caller: Use value.data
       Caller->>Stow: STOW_RELEASE(item_id, &value)
       Stow->>Stow: Free the reference or copy

Set Items
=========

Use :any:`STOW_SET` with a :any:`data_value_t` that includes the correct type tag.

.. code-block:: c

   data_value_t val = {
       .type = STOW_ITEM_TYPE_INT,
       .data.int_value = 42,
   };
   STOW_SET(STOW_ID_TEST_INT, val);

.. mermaid::

   %%{init: {"theme": "neutral"}}%%
   sequenceDiagram
       participant Caller
       participant Stow

       Caller->>Stow: STOW_SET(item_id, value)
       Stow->>Stow: Validate against item constraints
       Stow->>Stow: Set the new value
       Stow->>Stow: Notify subscribers
       Stow->>Caller: SUCCESS or error

It may be cumbersome to allocate a buffer for small buffer types. In this case, a :any:`STACK_BUFFER` can be used to create a stack-allocated :any:`buffer_t`:

.. code-block:: c

   STACK_BUFFER(bytes, 6);
   bytes->buf[0] = 0xAB;
   data_value_t val = {
       .type = STOW_ITEM_TYPE_BUFFER,
       .data.buffer_value = bytes,
   };
   STOW_SET(STOW_ID_TEST_BUFFER, val);

Subscriptions
=============

The Stow API allows modules to subscribe to items. The subscription callback will fire whenever the associated item is changed. Subscriptions can be by handle or copy:

``STOW_SUBSCRIPTION_HANDLE``
   Handle subscriptions simply notify a subscriber *that* and item changed, not what value it changed to. The callback receives only the item ID. Subscribers must read the current value via :any:`STOW_GET`. Notably, an item's value may change by the time a subscriber reads the current value. Use this when you only care about the latest value.

``STOW_SUBSCRIPTION_COPY``
   If guaranteed delivery is needed, copy subscriptions can be used. Copy subscriptions notify subscribers whenever an item's value changes, including the value that the item was changed to. The callback receives a copy of the value at the moment the update occurred. Use this for guaranteed delivery of every value.

.. code-block:: c

   void on_update(event_t* event)
   {
       struct stow_update_event_payload* payload =
           (struct stow_update_event_payload*)event->data.buf;
       LOG_INF("New value: %d", payload->value_copy.data.int_value);
   }

   static struct stow_subscription sub = {
       .mode = STOW_SUBSCRIPTION_COPY,
       .cb   = on_update,
   };

   stow_subscribe(STOW_ID_TEST_INT, &sub);

   // later:
   stow_unsubscribe(STOW_ID_TEST_INT, &sub);

If a subscription will be alive for the full lifetime of the app, a static subscription can be used via :any:`STOW_SUBSCRIPTION_DEFINE`. These subscriptions are stored in ROM rather than dynamically allocated.

.. important::

   Subscription callbacks run with interrupts masked, and possibly from an interrupt context. They must complete quickly and may not block. :any:`STOW_CALLBACK_DEFINE` is recommended for callbacks.

The debug options ``CONFIG_STOW_CALLBACK_TIME_ASSERT`` and ``CONFIG_STOW_CALLBACK_TIME_BUDGET_US`` can be used to ensure callbacks don't exceed the defined time budget.

Encode & Decode
===============

Items can be serialized to and from CBOR. Items are encoded when stored in non-volatile storage as well as when transmitted over the Stow Protocol. The value from :any:`STOW_DECODE` **must always** be released with :any:`STOW_RELEASE`. 

.. code-block:: c

   uint8_t buf[256];
   ZCBOR_STATE_E(encoder, 1, buf, sizeof(buf), 1);

   data_value_t val = {0};
   STOW_GET(STOW_ID_TEST_INT, &val);
   stow_encode(encoder, STOW_ID_TEST_INT, val);
   STOW_RELEASE(STOW_ID_TEST_INT, &val);

   ZCBOR_STATE_D(decoder, 1, buf, sizeof(buf), 1, 0);
   data_value_t decoded = {0};
   STOW_DECODE(decoder, STOW_ID_TEST_INT, &decoded);
   STOW_RELEASE(STOW_ID_TEST_INT, &decoded);

Custom Interface
================

Items may optionally override the default get, set, and validation. These function overrides are declared in the :doc:`schema </stow/schema>` by name and resolved at link time.

.. important::

   ``custom_get``, ``custom_set``, and ``custom_validate`` run with interrupts masked. They must be quick and may never block.

``custom_validate``
   Called after the default constraint check as an additional gate. Returns ``true`` to accept the value or ``false`` to reject it. Use sparingly; clients reading the Stow description will have no way to know about application-specific validation rules.

``custom_get``
   Replaces the default interface ``get``, after the permission check when called via :any:`stow_get_external`. This could be used to read a value on demand rather than reading from stored state.

``custom_set``
   Replaces the default interface ``set``, after the permission check (when called via :any:`stow_set_external`), constraint validation, and any ``custom_validate``. This to intercept writes and apply modifications before saving. ``custom_set`` can call the default ``set`` implementation after its custom logic to ensure the new value is set in the Stow.

**Example:**

.. code-block:: c

   static int latest_sensor_reading;

   int my_item_get(const struct stow_item_const_metadata *item, data_value_t *out_value)
   {
       out_value->type = STOW_ITEM_TYPE_INT;
       out_value->data.int_value = latest_sensor_reading;
       return SUCCESS;
   }

   int my_item_set(const struct stow_item_const_metadata* item, data_value_t value)
   {
      // Perform a modification (e.g. divide by 2)
      // Use care to ensure the value is always valid given the item's constraints
      value.int_value = value.int_value / 2;

      // Call the default set implementation to store the value
      item->interface->set(item->value_ptr, value);
      return SUCCESS;
   }

   bool my_item_validate(const struct stow_item_const_metadata *item, data_value_t value)
   {
      // Only allow even values
       return value.data.int_value % 2 == 0;
   }

Authentication
==============

Each Stow item has access controls for reading and writing values. The internal API (:any:`stow_set`, :any:`stow_get`, :any:`stow_subscribe`) takes no role parameter and performs no permission check. Internal firmware modules always have full access.

Callers that must be checked against an item's permissions use the external variants instead: :any:`stow_set_external`, :any:`stow_get_external`, and :any:`stow_subscribe_external`. The :doc:`Stow Protocol </stow/protocol>` is the primary example. Each function takes a bitfield of the caller's current roles, checks it against the item's permission mask, and delegates to the corresponding core function on success.

Each item's permissions are defined by a bitfield of roles. Roles can be defined in the :doc:`schema </stow/schema>`. If the caller has **any** of the allowed roles, it has access. A permission mask of ``STOW_ROLE_INTERNAL`` (``0``) marks an item as internal-only, so no external caller can access it.

Configuration
=============

.. code-block:: kconfig

   # Stow dependencies
   CONFIG_ZCBOR=y
   CONFIG_SETTINGS=y
   CONFIG_NVS=y
   CONFIG_SETTINGS_NVS=y
   CONFIG_FLASH=y
   CONFIG_FLASH_MAP=y

   # Enable the Stow library
   CONFIG_GANTRY_STOW=y

   # Maximum encoded size for items in persistent storage (bytes)
   CONFIG_STOW_ITEM_STORAGE_SIZE_MAX=1024

   # Delay before persistent items are saved to storage
   CONFIG_STOW_STORAGE_FLUSH_DELAY_MS=1000

   # Debug option to assert when callbacks exceed the time budget
   CONFIG_STOW_CALLBACK_TIME_ASSERT=y
   CONFIG_STOW_CALLBACK_TIME_BUDGET_US=200

API Reference
=============

.. doxygengroup:: stow
   :content-only: