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

- Set, Get, & Subscribe to Stow items
- Retrieving a full description of the Stow (so that client need not be aware of the ``stow.yaml`` in use by the firmware)
- Multiple concurrent external client sessions
- Set and get item values in bulk
- Role-based access controls at the item level
- Easy integration with arbitrary transport mediums and protocols

Use With Other Protocols
------------------------

Stow Protocol messages are encoded with `CBOR <https://cbor.io/>`_. The Stow Protocol uses Zephyr network buffers with user-defined head/tail room. This allows the Stow Protocol to be easily embedded into any arbitrary high-level protocols.

Messages
========

The Stow Protocol mostly follows a request-response pattern. However, some messages may arrive asynchronously (e.g. Update messages).

**Summary:**

.. list-table::
   :header-rows: 1
   :widths: 10 20 70

   * - Code
     - Name
     - Description
   * - ``0``
     - Version
     - Request the protocol version.
   * - ``1``
     - Version Response
     - Returns the current protocol version (currently ``1``).
   * - ``2``
     - Describe
     - Request a chunk of the Stow description starting at a given item ID.
   * - ``3``
     - Describe Response
     - A chunk of the CBOR-encoded item description with continuation info.
   * - ``4``
     - Get
     - Get the current value of an item by ID.
   * - ``5``
     - Get Response
     - Returns the item ID and its current value.
   * - ``6``
     - Set
     - Set the value of an item by ID.
   * - ``7``
     - Multi-Get
     - Get the values of multiple items in a single request.
   * - ``8``
     - Multi-Get Response
     - Returns a flat sequence of ID/value pairs for all requested items.
   * - ``9``
     - Multi-Set
     - Set the values of multiple items in a single request.
   * - ``10``
     - Subscribe
     - Subscribe to value change notifications for an item.
   * - ``11``
     - Unsubscribe
     - Unsubscribe from notifications for an item.
   * - ``12``
     - Update
     - Sent to the client when a subscribed item's value changes.
   * - ``13``
     - OK
     - Success response for operations without a dedicated response.
   * - ``14``
     - Error
     - Failure response with a numeric error code.

The message examples are shown using JSON array notation. All messages are CBOR-encoded in practice; use `cbor.me <https://cbor.me/>`_ to view the CBOR representation. Variable values are represented using fully capitalized snake case.

Version
-------

Request the protocol version. The client should send this to ensure a compatible protocol version prior to further communications.

.. code-block:: text

  [0]

Version Response
----------------

Sent in response to the Version request. It returns the protocol version (currently ``1``).

.. code-block:: text

  [1, VERSION]

Describe
--------

Request a chunk of the Stow description starting at ``START_ITEM_ID``. Pass ``0`` to start from the beginning. Use the ``NEXT_ITEM_ID`` from each Describe Response as the ``START_ITEM_ID`` for the following request. The description is complete when ``HAS_MORE`` is ``false``.

.. code-block:: text

  [2, START_ITEM_ID]

Describe Response
-----------------

Sent in response to a Describe request. Each response contains a byte string with a fragment of a larger CBOR-encoded description list. Reassemble all chunks in order before decoding. ``HAS_MORE`` indicates whether additional chunks remain.

The maximum size of the CBOR chunk is determined by the ``STOW_PROTOCOL_DESCRIBE_CHUNK_SIZE`` Kconfig option.

.. code-block:: text

   [3, NEXT_ITEM_ID, HAS_MORE, CBOR_CHUNK]

Get
---

Request the current value of an item. Will return ``Get Response`` or ``Error``.

.. code-block:: text

   [4, ITEM_ID]

Get Response
------------

Sent in response to a successful Get request. Contains the current value of the associated item ID.

.. code-block:: text

   [5, ITEM_ID, VALUE]

Set
---

Set the value of an item. Returns ``OK`` on success or ``Error`` on failure.

.. code-block:: text

   [6, ITEM_ID, VALUE]

Multi-Get
---------

Get the current values of multiple items in a single request. Returns a ``Multi-Get Response`` on success, or an ``Error`` if any item could not be retrieved. 

The maximum number of items in a single request is determined by the ``STOW_PROTOCOL_MULTI_MAX_ITEMS`` Kconfig option.

.. code-block:: text

   [7, FIRST_ITEM_ID, SECOND_ITEM_ID, ...]

Multi-Get Response
------------------

Sent in response to a valid Multi-Get request. The response is a flat sequence of alternating ID/value pairs.

.. code-block:: text

   [8, FIRST_ITEM_ID, FIRST_VALUE, SECOND_ITEM_ID, SECOND_VALUE, ...]

Multi-Set
---------

Set multiple items in one request. Returns ``OK`` if all items were set successfully. On the first failure an ``Error`` is returned; subsequent items are not set. The client can read the Stow to determine which items were applied.

The maximum number of items in a single request is determined by the ``STOW_PROTOCOL_MULTI_MAX_ITEMS`` Kconfig option.

.. code-block:: text

   [9, FIRST_ITEM_ID, FIRST_VALUE, SECOND_ITEM_ID, SECOND_VALUE, ...]

Subscribe
---------

Subscribe to receive ``Update`` messages when the associated item is set.Returns ``OK`` or ``Error``.

.. code-block:: text

   [10, ITEM_ID]

Unsubscribe
-----------

Unsubscribe to stop receiving ``Update`` messages for the associated item. Returns ``OK`` or ``Error``.

.. code-block:: text

   [11, ITEM_ID]

Update
------

Sent to the client when a subscribed item's value is set.

.. code-block:: text

   [12, ITEM_ID, VALUE]

OK
--

Sent in response to successful operations that do no have a dedicated response.

.. code-block:: text

   [13]

Error
-----

Sent in response to failed operations. See :ref:`error-codes` for possible errors.

.. code-block:: text

   [14, ERROR_CODE]

.. _error-codes:

Error Codes
-----------

.. list-table::
   :header-rows: 1
   :widths: 10 30 60

   * - Code
     - Name
     - Description
   * - ``1``
     - ``MALFORMED_MSG``
     - The message was malformed.
   * - ``2``
     - ``UNKNOWN_MSG``
     - The message code was not recognized.
   * - ``3``
     - ``INVALID_ITEM``
     - The item ID was invalid.
   * - ``4``
     - ``OUT_OF_MEMORY``
     - Not enough memory to handle the message.
   * - ``5``
     - ``PERMISSION_DENIED``
     - The client's role(s) were insufficient.
   * - ``6``
     - ``UNKNOWN``
     - An unknown error occurred (should never occur).

Client Interaction
==================

Connecting to the Stow
----------------------

External clients should verify the protocol version then check the Stow hash to see if they have the Stow description cached. The Stow hash is always the first item (with an ID of ``0``). The Python Stow client provides and example of caching descriptions.

.. mermaid::

    %%{init: {"theme": "neutral"}}%%
    sequenceDiagram
        participant C as Client
        participant S as Stow Server

        C->>S: Version [0]
        S->>C: Version Response [1, 1]
        C->>C: Verify the version is 1
        C->>S: Get [4, 0]
        S->>C: Get Response [5, <hash>]
        C->>C: Check if the Stow is cached based on the hash

If the description is not yet cached, the client should retrieve the Stow description.

Describing the Stow
-------------------

The client pages through the Stow description by repeatedly sending ``Describe`` with the ``NEXT_ITEM_ID`` returned by each response. The loop ends when ``HAS_MORE`` is ``false``.

.. mermaid::

    %%{init: {"theme": "neutral"}}%%
    sequenceDiagram
        participant C as Client
        participant S as Stow Server

        C->>S: Describe [2, 0]
        S->>C: Describe Response [3, next_id, true, chunk_1]
        C->>S: Describe [2, next_id]
        S->>C: Describe Response [3, _, false, chunk_N]
        Note over C: Concatenate chunks and decode the CBOR list

Getting and Setting Items
-------------------------

.. mermaid::

   %%{init: {"theme": "neutral"}}%%
   sequenceDiagram
       participant C as Client
       participant S as Stow Server

       C->>S: Get [4, item_id]
       S->>C: Get Response [5, item_id, value]

       C->>S: Set [6, item_id, new_value]
       S->>C: OK [13]

       C->>S: Multi-Get [7, id_1, id_2]
       S->>C: Multi-Get Response [8, id_1, value_1, id_2, value_2]

       C->>S: Multi-Set [9, id_1, value_1, id_2, value_2]
       S->>C: OK [13]

Subscribing and Receiving Updates
----------------------------------

Once subscribed, the server pushes ``Update`` messages to the client whenever the item's value changes. The client must unsubscribe explicitly.

.. mermaid::

   %%{init: {"theme": "neutral"}}%%
   sequenceDiagram
       participant C as Client
       participant S as Stow Server

       C->>S: Subscribe [10, item_id]
       S->>C: OK [13]

       Note over S: Item value changes
       S-->>C: Update [12, item_id, new_value]
       
       C->>S: Unsubscribe [11, item_id]
       S->>C: OK [13]

       Note over S: Item value changes again, no update sent


Stow Description
================

Once all ``Describe Response`` chunks are concatenated and decoded, the result is a CBOR list of item descriptions.

Description Format
------------------

.. code-block:: text

   {
     "id":          ITEM_ID,
     "name":        "item_name",
     "categories":  ["category", ...],
     "storage":     "Ephemeral" | "Persistent" | "TOFU",
     "read_perm":   ["RoleName", ...],
     "write_perm":  ["RoleName", ...],
     "type":        "Enum" | "Int" | "Float" | "String" | "Byte Array" | "Buffer" | "Struct",
     "default":     DEFAULT_VALUE,
     "constraints": TYPE_SPECIFIC_CONSTRAINTS
   }

The ``id`` corresponds to the ``ITEM_ID`` used in Get, Set, Subscribe, and Update messages. IDs may change across firmware versions; use the ``name`` field as the stable, human-readable identifier.

An empty ``read_perm`` or ``write_perm`` array means the item is firmware-only and cannot be accessed by any external client. Otherwise, a client whose session includes any of the listed role names is granted access.

Constraints by Type
-------------------

**Enum**

A list of possible values paired with descriptive names.

.. code-block:: json

   [
     {"value": 0, "name": "DISCONNECTED"},
     {"value": 1, "name": "CONNECTED"}
   ]

**Int / Float**

Minimum and maximum bounds encoded as ``int32`` or ``float32`` respectively.

.. code-block:: json

   {"min": -100, "max": 100}

**String / Byte Array / Buffer**

Minimum and maximum length of the buffer.

.. code-block:: json

   {"min_len": 0, "max_len": 64}

**Struct**

A list of field descriptors. Each field has its own name, type, and constraints. Fields can be nested structs.

.. code-block:: json

   [
     {"name": "CS",    "type": "Enum",       "constraints": [{"value": 0, "name": "OFF"}, {"value": 1, "name": "ON"}]},
     {"name": "Bytes", "type": "Byte Array", "constraints": {"min_len": 0, "max_len": 6}}
   ]
