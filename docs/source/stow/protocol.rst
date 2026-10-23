=============
Stow Protocol
=============

Overview
========

The Stow Protocol allows external clients to discover and interact with items in the Stow.

Gantry provides an implementation of the Stow Protocol where the firmware acts as a server containing a Stow. External clients can use the Stow protocol to interact with the firmware's Stow. A Python client implementation is provided as an example available under ``python/stow-client``.

Key Features
------------

Notably, the Stow Protocol implementation supports:

- Set, Get, & Subscribe to Stow items, one or several per request
- Retrieving a full description of the Stow, or of selected items (so that client need not be aware of the ``stow.yaml`` in use by the firmware)
- Multiple concurrent external client sessions, with several requests in flight per session
- Role-based access controls at the item level
- Easy integration with arbitrary transport mediums and protocols

Transports
----------

Stow Protocol messages are encoded with `CBOR <https://cbor.io/>`_. Stow does not handle framing or delivery; a transport does, through the contract in ``gantry/stow/stow_transport.h``. The transport opens a session on ``stow_protocol_endpoint`` when a peer connects, with the roles that peer holds, passes each complete message to ``receive`` and closes the session when the peer leaves. Stow sends messages back through the transport's ``send``.

Each transport message carries exactly one Stow message. A transport must deliver any message up to the size of its pool's buffers, which ``hello-max-message`` relies on.

``CONFIG_STOW_COBS_TRANSPORT`` provides ``stow_cobs_transport``, which carries Stow messages as COBS frames over a byte stream such as UART. A link carries a server session for the peer, set with ``.server`` and opened by ``stow_cobs_transport_open``, and a client session the app opens with ``stow_cobs_transport_open_client``. It is the framing used by the Python client's serial transport and a good starting point for writing a new transport.

An application that authenticates a peer in-band can change a session's roles with ``stow_protocol_session_set_roles``. Subscriptions the new roles can no longer read are revoked.

Messages
========

Every message is an array ``[CODE, TAG, ...]``. Hello (code ``0``) and the version at the start of its response are fixed across protocol versions; everything else may change. Every code has a fixed direction, requests from client to server and the rest from server to client, and new codes must keep that so one stream can carry a session in each direction.

The Stow Protocol mostly follows a request-response pattern. However, Update messages arrive unsolicited.

The :ref:`definition <stow-cddl>` at the end of this page gives every message, error code and item description exactly, in `CDDL <https://www.rfc-editor.org/rfc/rfc8610>`_; the sections below explain how they behave. Examples use CBOR diagnostic notation, which reads like JSON; use `cbor.me <https://cbor.me/>`_ to view the CBOR representation.

.. list-table::
   :header-rows: 1
   :widths: 25 75

   * - Message
     - Description
   * - Hello
     - Request the protocol version, the server's limits and the session's roles.
   * - Hello Response
     - Returns the protocol version (currently ``1``), the server's limits, the ``StowHash``, the session's roles and the role names.
   * - Describe
     - Request a chunk of the Stow description starting at a given item ID, optionally for named items only.
   * - Describe Response
     - A chunk of CBOR-encoded item descriptions and the item to continue from.
   * - Get
     - Get the current values of one or more items.
   * - Get Response
     - Returns the ID and current value of each requested item.
   * - Set
     - Set the values of one or more items.
   * - Subscribe
     - Subscribe to value notifications for one or more items.
   * - Unsubscribe
     - Unsubscribe from notifications for one or more items.
   * - Update
     - Sent to the client when a subscribed item is set.
   * - OK
     - Success response for operations without a dedicated response.
   * - Error
     - Failure response with an error code, and the failing item when known.

Tags
----

Every request carries a tag as its second element: an unsigned integer from ``0`` to ``23`` chosen by the client. Every response echoes the tag of the request it answers, so a client can have several requests in flight and match each response to its request. A client should not reuse a tag while a response to it may still arrive.

Update is unsolicited and its tag is ``null``.

Delivery
--------

Every request the server handles gets exactly one response, either its dedicated response, ``OK`` or ``Error``.

The server does not acknowledge delivery or retry anything. A request is dropped without a response when the server is too busy to take it, for example when its queue or the transport's buffers are full, so clients should apply a timeout to every request. A response or Update the transport cannot send is dropped as well.

Updates are best effort. A client that relies on an item's value should Subscribe and then Get it, Get it again after a reconnect, and poll items where a stale value would matter.

Hello
-----

Request the protocol version, the server's limits and the roles of the session. The client sends it once when the session starts, before any other request.

The response starts with the protocol version, currently ``1``; a client that does not support it should stop. The map that follows holds:

- ``hello-max-message``: the largest message, in bytes, the server can send on this session. Clients keep every request within it.
- ``hello-stow-hash``: the ``StowHash`` item's value, used to cache the Stow description.
- ``hello-roles``: the bitmask of the roles the session holds.
- ``hello-role-names``: the name of every role, indexed by its bit.

Clients should ignore keys they do not recognize.

Describe
--------

Request a chunk of the Stow description starting at item ``start``. Pass ``0`` to start from the beginning, then the ``next`` from each Describe Response. The description is complete when ``next`` is ``null``.

The optional ``names`` list restricts the description to the named items. Unknown names are ignored, and a list that matches no item returns an empty chunk.

Any session can describe every item; permissions are enforced when an item is accessed.

The response's ``chunk`` holds whole item descriptions as a `CBOR sequence <https://www.rfc-editor.org/rfc/rfc8742>`_, so each chunk can be decoded on its own. The number of items per chunk depends on the size of the session's buffers. When the next item cannot fit an otherwise empty response, the server answers ``err-too-large`` naming that item.

Get
---

Request the current values of one or more items. Returns a Get Response with each item and its value, in the order requested, or an ``Error`` for the first item that could not be read.

Set
---

Set the values of one or more items. Returns ``OK`` if every item was set. Items are set in order and the first failure stops processing with an ``Error``; every item before the failing one was set and the rest were not.

Subscribe
---------

Subscribe to receive Update messages when one or more items are set. Returns ``OK``, or an ``Error`` for the first item that could not be subscribed; the subscriptions before it are active. Subscribing to an item twice is harmless.

Unsubscribe
-----------

Unsubscribe to stop receiving Update messages for one or more items. Returns ``OK`` or ``Error``. Unsubscribing from an item that is not subscribed is harmless.

Error
-----

Sent in response to failed operations. See :ref:`error-codes` for possible errors.

``item`` names the item that failed, when there is one. ``index`` is the zero-based position of an entry that failed as it was applied; the entries before it took effect and the rest did not. A request that fails before anything is applied, for example because it is malformed, carries no ``index`` and took no effect. Optional elements that do not apply are omitted. The tag is ``null`` when the server could not read the request's tag.

.. _error-codes:

Error Codes
-----------

.. list-table::
   :header-rows: 1
   :widths: 30 70

   * - Name
     - Description
   * - ``err-malformed-msg``
     - The message was malformed.
   * - ``err-unknown-msg``
     - The message code was not recognized.
   * - ``err-invalid-item``
     - The item ID was invalid.
   * - ``err-out-of-memory``
     - Not enough memory to handle the message.
   * - ``err-permission-denied``
     - The client's role(s) were insufficient.
   * - ``err-unknown``
     - An unknown error occurred (should never occur).
   * - ``err-invalid-value``
     - The value broke the item's constraints or custom validator.
   * - ``err-already-set``
     - The trust-on-first-use item has already been set.
   * - ``err-too-large``
     - The response does not fit the server's buffers.

Client Interaction
==================

Connecting to the Stow
----------------------

External clients send Hello to verify the protocol version and learn the server's limits, the session's roles and the ``StowHash``. The hash tells the client whether it already has the Stow description cached. The Python Stow client provides an example of caching descriptions.

.. mermaid::

    %%{init: {"theme": "neutral"}}%%
    sequenceDiagram
        participant C as Client
        participant S as Stow Server

        C->>S: Hello [0, 1]
        S->>C: Hello Response [1, 1, 1, {..., 1: hash, ...}]
        C->>C: Verify the version is 1
        C->>C: Check if the Stow is cached based on the hash

If the description is not yet cached, the client should retrieve the Stow description.

Describing the Stow
-------------------

The client pages through the Stow description by repeatedly sending ``Describe`` with the ``next`` returned by each response. The loop ends when ``next`` is ``null``.

.. mermaid::

    %%{init: {"theme": "neutral"}}%%
    sequenceDiagram
        participant C as Client
        participant S as Stow Server

        C->>S: Describe [2, 2, 0]
        S->>C: Describe Response [3, 2, next_id, chunk_1]
        C->>S: Describe [2, 3, next_id]
        S->>C: Describe Response [3, 3, null, chunk_N]
        Note over C: Decode the item descriptions in each chunk

Getting and Setting Items
-------------------------

.. mermaid::

   %%{init: {"theme": "neutral"}}%%
   sequenceDiagram
       participant C as Client
       participant S as Stow Server

       C->>S: Get [4, 4, id_1, id_2]
       S->>C: Get Response [5, 4, id_1, value_1, id_2, value_2]

       C->>S: Set [6, 5, id_1, new_value]
       S->>C: OK [10, 5]

       C->>S: Set [6, 6, id_1, value_1, id_2, bad_value]
       S->>C: Error [11, 6, err-invalid-value, id_2, 1]

Subscribing and Receiving Updates
---------------------------------

Once subscribed, the server pushes ``Update`` messages to the client whenever the item is set. The client must unsubscribe explicitly, or close the session.

.. mermaid::

   %%{init: {"theme": "neutral"}}%%
   sequenceDiagram
       participant C as Client
       participant S as Stow Server

       C->>S: Subscribe [7, 7, id_1, id_2]
       S->>C: OK [10, 7]

       Note over S: Item value is set
       S-->>C: Update [9, null, id_1, new_value]

       C->>S: Unsubscribe [8, 8, id_1, id_2]
       S->>C: OK [10, 8]

       Note over S: Item value is set again, no update sent

Golden Vectors
--------------

``protocols/stow/vectors.yaml`` holds exchanges between a client and a server, written in CBOR diagnostic notation and recorded against ``protocols/stow/schema.yaml``. The server replays every exchange, and the C and Python clients are checked against them. ``just vectors`` validates every message against the CDDL and checks that every request and response kind has a vector. ``tools/protocol_spec/README.md`` describes the format and the rules for playing a vector. Client authors outside the repo can export the vectors with ``protocol-spec json protocols/stow --output vectors.json``.

Stow Description
================

Each Describe Response chunk holds a sequence of item description maps. An item's ``desc-type`` sets how its ``desc-default`` and its values are encoded and the shape of its ``desc-constraints``.

``desc-id`` is the item ID used in Get, Set, Subscribe and Update messages. IDs may change across firmware versions; use ``desc-name`` as the stable identifier.

Bit ``n`` of ``desc-read-perm`` and ``desc-write-perm`` is the role at index ``n`` of the Hello Response's ``hello-role-names``. A client whose session holds any role in the mask is granted access. A mask of ``0`` means the item is firmware-only and cannot be accessed by any external client.

.. _stow-cddl:

Definition
==========

``protocols/stow/stow.cddl`` is the normative definition of the protocol.

.. literalinclude:: ../../../protocols/stow/stow.cddl
   :language: text
