===========
Stow Client
===========

Overview
========

The Stow client lets firmware read, write and subscribe to the Stow of a peer device. It runs over the same transport contract as the Stow protocol server (see :doc:`protocol`), and values use the same ``data_value_t`` and per-type codecs as local Stow items.

The client is enabled with ``CONFIG_STOW_CLIENT``, independently of the local Stow (``CONFIG_GANTRY_STOW``) and the server (``CONFIG_STOW_PROTOCOL``). A client-only build needs no ``stow.yaml`` of its own.

Building Against a Peer
=======================

A client is built against the peer's schema, or only the items it uses:

.. code-block:: cmake

   gantry_stow_client_generate(
     PREFIX     device
     YAML       ${DEVICE_DIR}/stow.yaml
     FRAGMENTS  ${DEVICE_DIR}/ble.stow.yaml
     ITEMS      BatteryLevel LedMode
   )

The peer's schema is composed and validated as in the peer's own build. ``generated_device_stow_client.h`` holds an ID per item (``DEVICE_ID_BATTERY_LEVEL``), the table ``device_stow_client_schema``, constraint and role constants, and the peer's enums and structs, all under the prefix. A peer enum or struct identical to one in the local Stow is the local type, so ``device_Point_t`` is ``Point_t`` and needs no cast. Naming an item the peer's schema does not have fails the build.

Connecting
==========

A client endpoint holds the schema peers are checked against. Transports open sessions on it, and each session is a peer. Over COBS, the link's server is optional and the app opens the client session once the link is up:

.. code-block:: c

   STOW_CLIENT_ENDPOINT_DEFINE(device_client, &device_stow_client_schema, 0);

   static struct stow_cobs_transport device_link;
   static const struct stow_cobs_transport_config device_link_config = {
       .server = &stow_protocol_endpoint,
       .session_id = DEVICE_SESSION,
       /* pools and write callback */
   };

   stow_cobs_transport_init(&device_link, &device_link_config);
   stow_cobs_transport_open(&device_link);
   stow_cobs_transport_open_client(&device_link, &device_client.endpoint);

The app opens and closes sessions through its transport (``stow_cobs_transport_close_client``, or ``stow_cobs_transport_close`` for the whole link); the client never does. A device can be server and client on one link, and when a client-enabled peer has no server, it answers the Hello with UNKNOWN_MSG and ``connected`` reports ``-EPROTO``. Up to ``CONFIG_STOW_CLIENT_MAX_PEERS`` peers can exist at once, counting closed peers still referenced by the app or by events.

When a session opens, the client verifies the peer: it sends Hello, then Describe filtered to the bound names, and matches items by name. An item is available when its type and constraints match exactly, missing when the peer lacks it and incompatible otherwise. Requests naming a missing or incompatible item fail with ``-ENOTSUP``. An item whose description is too large for the peer to send is incompatible. Peer schemas must follow the :ref:`compatibility rule <stow-compatibility>`.

Hooks are registered statically and run on the client thread. A peer is borrowed inside the ``connected`` and ``disconnected`` hooks; take a reference with ``stow_client_peer_ref`` to keep it, and release it with ``stow_client_peer_unref``.

.. code-block:: c

   static struct stow_client_peer* device;

   static void on_connected(struct stow_client_peer* peer, int err)
   {
       if (err != SUCCESS)
       {
           LOG_ERR("Device could not be verified (%d)", err);
           stow_cobs_transport_close_client(&device_link);
           return;
       }
       device = stow_client_peer_ref(peer);
   }

   static void on_disconnected(struct stow_client_peer* peer)
   {
       stow_client_peer_unref(&device);
   }

   STOW_CLIENT_CB_DEFINE(app_hooks) = {
       .connected = on_connected,
       .disconnected = on_disconnected,
   };

``disconnected`` follows only a successful ``connected``, and a referenced peer whose session closed answers every call with ``-ENOTCONN``.

Making Requests
===============

Get, Set, Subscribe and Unsubscribe take one or more items, and each has a ``_one`` form for a single item. Blocking calls must not run in interrupts or on the client thread. Release each value from a Get with ``STOW_CLIENT_RELEASE``.

.. code-block:: c

   stow_client_subscribe_one(device, DEVICE_ID_BATTERY_LEVEL);

   struct stow_client_value level;
   if (stow_client_get_one(device, DEVICE_ID_BATTERY_LEVEL, &level) == SUCCESS)
   {
       LOG_INF("Battery %d%%", level.value.data.int_value);
       STOW_CLIENT_RELEASE(&device_client, &level);
   }

   stow_client_set_one(device, DEVICE_ID_LED_MODE, (data_value_t){ .type = STOW_ITEM_TYPE_ENUM, .data.int_value = device_LedMode_ON });

Each call has an ``_async`` form whose callback runs on the client thread with a ``stow_client_result_event``. The event holds the error, the entry that failed and, for a Get, the values, and a callback keeps it with ``EVENT_REF``. The event is allocated when the request is queued, so a request with no event free fails at once with ``-ENOBUFS`` and a queued request always completes. A blocking Set that fails part way does not say which entry failed; the result's ``index`` does.

A request must fit both the peer's ``max_message`` (``stow_client_peer_info``) and the client's own transport buffers, or it fails with ``-EMSGSIZE``. Errors from the peer arrive as errnos, for example ``-EINVAL`` for a rejected value and ``-EACCES`` for missing roles; a response the client cannot make sense of fails with ``-ENOMSG``.

Timeouts
--------

A request the peer drops ends in ``-ETIMEDOUT`` after the endpoint's timeout. A request the transport or the client's queue refuses fails at once with ``-EBUSY``.

Updates
-------

Each Update for a subscribed item reaches every ``update`` hook as a ``stow_client_update_event``, which owns the value and a reference to the peer. As with :any:`STOW_CALLBACK_DEFINE`, a hook keeps the event and posts it to its module's queue, and the module releases it when done:

.. code-block:: c

   static void on_update(event_t* event)
   {
       EVENT_REF(event);
       if (k_msgq_put(&device_queue, &event, K_NO_WAIT) != 0)
       {
           EVENT_UNREF(&event);
       }
   }

   STOW_CLIENT_CB_DEFINE(device_hooks) = {
       .update = on_update,
   };

   // In the module's thread
   event_t* event = NULL;
   k_msgq_get(&device_queue, &event, K_FOREVER);
   const struct stow_client_update_payload* update = (const struct stow_client_update_payload*)event->data.buf;
   if (update->value.id == DEVICE_ID_BATTERY_LEVEL)
   {
       LOG_INF("Battery %d%%", update->value.value.data.int_value);
   }
   EVENT_UNREF(&event);

Events come from the memory pools, so size them for the Updates and results in flight. An event holds a reference to its peer, so a closed peer's slot stays taken until its events are released. Updates are best effort, and one that finds no event free is dropped. Get an item after subscribing and after reconnecting, and poll items where a stale value matters. Subscriptions end with the session.
