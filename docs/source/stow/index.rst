Stow
====

The Stow is the central data store for a Gantry application. All system state, configuration, and device information lives here. Any firmware module or connected external device can read, write, or subscribe to Stow items.

Items are defined at compile time via a ``stow.yaml`` schema file. The code generator produces typed C stubs so access is consistent and type-safe.

.. toctree::
   :maxdepth: 1

   Types <types>
   Schema <schema>
   Protocol <protocol>

Initialization
--------------

Call ``stow_init()`` once at startup before accessing any items. This loads persistent values from flash.

.. code-block:: c

   stow_init();

Reading Items
-------------

Use ``STOW_GET`` to read an item's current value. Always call ``STOW_RELEASE`` when finished — types that return pointers (strings, buffers, structs) are reference-counted and must be released.

.. code-block:: c

   data_value_t val = {0};
   STOW_GET(AUTH_INTERNAL, STOW_ID_DEVICE_NAME, &val);
   LOG_INF("Device name: %s", val.data.string_value);
   STOW_RELEASE(STOW_ID_DEVICE_NAME, &val);

Writing Items
-------------

Use ``STOW_SET`` with a ``data_value_t`` that includes the type tag.

.. code-block:: c

   data_value_t val = {
       .type = STOW_ITEM_TYPE_INT,
       .data.int_value = 42,
   };
   STOW_SET(AUTH_INTERNAL, STOW_ID_TEST_INT, val);

For buffer types, use ``STACK_BUFFER`` to create a stack-allocated ``buffer_t``:

.. code-block:: c

   STACK_BUFFER(bytes, 6);
   bytes->buf[0] = 0xAB;
   data_value_t val = {
       .type = STOW_ITEM_TYPE_BUFFER,
       .data.buffer_value = bytes,
   };
   STOW_SET(AUTH_INTERNAL, STOW_ID_TEST_BUFFER, val);

Subscriptions
-------------

Subscribe to receive a callback whenever an item changes. Two modes are available:

``STOW_SUBSCRIPTION_HANDLE``
   The callback receives only the item ID. The subscriber reads the current value itself via ``STOW_GET``. Use this when you only care about the latest value.

``STOW_SUBSCRIPTION_COPY``
   The callback receives a copy of the value at the moment the update occurred. Use this for guaranteed delivery of every value.

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

   stow_subscribe(AUTH_INTERNAL, STOW_ID_TEST_INT, &sub);

   // later:
   stow_unsubscribe(STOW_ID_TEST_INT, &sub);

CBOR Encode/Decode
------------------

Items can be serialized to and from CBOR for transmission over the Stow Protocol.

.. code-block:: c

   uint8_t buf[256];
   ZCBOR_STATE_E(encoder, 1, buf, sizeof(buf), 1);

   data_value_t val = {0};
   STOW_GET(AUTH_INTERNAL, STOW_ID_TEST_INT, &val);
   stow_encode(encoder, STOW_ID_TEST_INT, val);
   STOW_RELEASE(STOW_ID_TEST_INT, &val);

   ZCBOR_STATE_D(decoder, 1, buf, sizeof(buf), 1, 0);
   data_value_t decoded = {0};
   STOW_DECODE(decoder, STOW_ID_TEST_INT, &decoded);
   STOW_RELEASE(STOW_ID_TEST_INT, &decoded);

Authentication
--------------

Every ``stow_get`` and ``stow_set`` call requires an auth level. The item schema defines the minimum level needed to read or write each item.

.. list-table::
   :header-rows: 1
   :widths: 25 75

   * - Level
     - Description
   * - ``AUTH_ANY``
     - No authentication required
   * - ``AUTH_SESSION``
     - Authenticated session required
   * - ``AUTH_DEV``
     - Developer session required
   * - ``AUTH_INTERNAL``
     - Firmware internal use only
   * - ``AUTH_NONE``
     - No access permitted

Configuration
-------------

.. code-block:: kconfig

   CONFIG_GANTRY_STOW=y
   CONFIG_ZCBOR=y
   CONFIG_SETTINGS=y
   CONFIG_NVS=y
   CONFIG_SETTINGS_NVS=y
   CONFIG_FLASH=y
   CONFIG_FLASH_MAP=y

   # Maximum encoded size for items in persistent storage (bytes)
   CONFIG_STOW_ITEM_STORAGE_SIZE_MAX=1024

API Reference
-------------

.. doxygengroup:: stow
   :content-only:
