Stow Protocol
=============

The Stow Protocol lets external devices interact with the Stow over any byte-stream transport. All messages are encoded with `CBOR <https://cbor.io/>`_. Each message is an array where the first element is a numeric command or response code.

You can view CBOR representations at `cbor.me <https://cbor.me/>`_.

A Python client library for the Stow Protocol is available under ``python/stow-client/``.

Message Summary
---------------

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
     - Returns the current protocol version (always ``1``).
   * - ``2``
     - Describe
     - Start a chunked description of all Stow items.
   * - ``3``
     - Describe Next
     - Request the next description chunk.
   * - ``4``
     - Describe Response
     - A chunk of the CBOR-encoded item description.
   * - ``5``
     - Get
     - Get the current value of an item by ID.
   * - ``6``
     - Get Response
     - Returns the item ID and its current value.
   * - ``7``
     - Set
     - Set the value of an item by ID.
   * - ``8``
     - Subscribe
     - Subscribe to updates for an item.
   * - ``9``
     - Unsubscribe
     - Unsubscribe from an item.
   * - ``10``
     - Update
     - Sent to the client when a subscribed item changes.
   * - ``11``
     - OK
     - Success response for operations without a dedicated response.
   * - ``12``
     - Error
     - Failure response with a text error description.

Message Formats
---------------

**Version**

.. code-block:: json

   [0]

**Version Response**

.. code-block:: json

   [1, <version>]

**Describe / Describe Next**

Send ``[2]`` to start. Keep sending ``[3]`` until a Describe Response returns an empty byte string.

.. code-block:: json

   [2]
   [3]

**Describe Response**

Each chunk is a byte string containing a fragment of a larger CBOR-encoded description list. Reassemble all chunks before decoding.

.. code-block:: json

   [4, <byte string>]

**Get / Get Response**

.. code-block:: json

   [5, <item id>]
   [6, <item id>, <value>]

**Set**

.. code-block:: json

   [7, <item id>, <value>]

Returns ``OK`` on success or ``Error`` on failure.

**Subscribe / Unsubscribe**

.. code-block:: json

   [8, <item id>]
   [9, <item id>]

Both return ``OK`` or ``Error``.

**Update**

Sent to the client when a subscribed item's value changes.

.. code-block:: json

   [10, <item id>, <value>]

**OK / Error**

.. code-block:: json

   [11]
   [12, "<error message>"]

Stow Description
----------------

Once all Describe Response chunks are combined, the result is a CBOR-encoded list of item descriptions. Each entry has the following shape:

.. code-block:: json

   {
     "id":          <numeric item ID>,
     "name":        "<item name>",
     "categories":  ["<category>", ...],
     "storage":     "<Ephemeral | Persistent | TOFU>",
     "read_perm":   "<Any | Session | Dev | No access>",
     "write_perm":  "<Any | Session | Dev | No access>",
     "type":        "<Enum | Int | Float | String | Byte Array | Buffer | Struct>",
     "default":     <default value>,
     "constraints": <type-specific constraints>
   }

The ``id`` in a description corresponds to the ``<item id>`` used in Get, Set, Subscribe, and Update messages. IDs may change across firmware versions; use the ``name`` field as the stable, human-readable identifier.

Constraints by Type
-------------------

**Enum**

.. code-block:: json

   [{"value": 0, "name": "DISCONNECTED"}, {"value": 1, "name": "CONNECTED"}]

**Int / Float**

.. code-block:: json

   {"min": -100, "max": 100}

**String / Byte Array / Buffer**

.. code-block:: json

   {"min_len": 0, "max_len": 64}

**Struct**

A list of field descriptors. Each field has its own name, type, and constraints. Fields can be nested structs.

.. code-block:: json

   [
     {"name": "CS",    "type": "Enum",       "constraints": [...]},
     {"name": "Bytes", "type": "Byte Array", "constraints": {"min_len": 0, "max_len": 6}}
   ]
