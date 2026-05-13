Stow Types
==========

Each Stow item has a type that determines how its value is stored, validated, and serialized. The type is specified in the ``stow.yaml`` schema and encoded in the generated ``data_value_t`` union.

Type Overview
-------------

.. list-table::
   :header-rows: 1
   :widths: 20 20 60

   * - Type
     - ``data_value_t`` field
     - Description
   * - ``ENUM``
     - ``data.int_value``
     - Named integer values. Constraints list valid values and their names.
   * - ``INT``
     - ``data.int_value``
     - 32-bit signed integer with min/max constraints.
   * - ``FLOAT``
     - ``data.float_value``
     - 32-bit float with min/max constraints.
   * - ``STRING``
     - ``data.string_value``
     - Null-terminated string with min/max length constraints.
   * - ``BYTE_ARRAY``
     - ``data.buffer_value``
     - Fixed-length byte array. Length is fixed and must match ``min_len == max_len``.
   * - ``BUFFER``
     - ``data.buffer_value``
     - Variable-length byte buffer with min/max length constraints.
   * - ``STRUCT``
     - ``data.buffer_value``
     - Composite type with named fields. Fields can be any type, including nested structs.

Scalar Types (ENUM, INT, FLOAT)
--------------------------------

Scalar values are stored directly in the ``data_value_t`` union. No release is needed after ``STOW_GET``, though ``STOW_RELEASE`` is still safe to call.

.. code-block:: c

   data_value_t val = {0};
   STOW_GET(AUTH_INTERNAL, STOW_ID_TEST_INT, &val);
   LOG_INF("int: %d", val.data.int_value);
   STOW_RELEASE(STOW_ID_TEST_INT, &val);  // safe no-op for scalars

For enums, you can look up the name of a value using ``enum_get_name_from_value``:

.. code-block:: c

   data_value_t val = {0};
   STOW_GET(AUTH_INTERNAL, STOW_ID_TEST_ENUM, &val);
   char* name = NULL;
   enum_get_name_from_value(
       &g_stow_const_metadata[STOW_ID_TEST_ENUM].constraints,
       val.data.int_value,
       &name);
   LOG_INF("enum: %d (%s)", val.data.int_value, name);
   STOW_RELEASE(STOW_ID_TEST_ENUM, &val);

Buffer Types (STRING, BYTE_ARRAY, BUFFER)
------------------------------------------

These types return a pointer to a ``buffer_t`` in ``data.buffer_value``. Always call ``STOW_RELEASE`` to release the reference.

.. code-block:: c

   data_value_t val = {0};
   STOW_GET(AUTH_INTERNAL, STOW_ID_TEST_BYTES, &val);
   LOG_HEXDUMP_INF(val.data.buffer_value->buf,
                   val.data.buffer_value->len,
                   "bytes");
   STOW_RELEASE(STOW_ID_TEST_BYTES, &val);

To write a buffer type, create a ``buffer_t`` with the data. Use ``STACK_BUFFER`` for stack-allocated buffers:

.. code-block:: c

   STACK_BUFFER(data, 6);
   data->buf[0] = 0xDE;
   data->buf[1] = 0xAD;
   data_value_t val = {
       .type = STOW_ITEM_TYPE_BUFFER,
       .data.buffer_value = data,
   };
   STOW_SET(AUTH_INTERNAL, STOW_ID_TEST_BUFFER, val);

Struct Types
------------

Struct values are stored as a ``buffer_t`` where the buffer contains the packed fields in declaration order. The field layout and types are defined in ``stow.yaml`` and reflected in the generated code.

Reading a struct returns a ``buffer_t*`` in ``data.buffer_value``. Cast the ``buf`` pointer to access individual fields.

Storage Types
-------------

Each item has a storage type that controls persistence:

.. list-table::
   :header-rows: 1
   :widths: 25 75

   * - Storage
     - Behavior
   * - ``EPHEMERAL``
     - Resets to default on reboot. Stored in RAM only.
   * - ``PERSISTENT``
     - Saved to flash on every write. Restored on boot via ``stow_init()``.
   * - ``TOFU``
     - Write-once. Can be written once while at the default value. Permanent after that.
