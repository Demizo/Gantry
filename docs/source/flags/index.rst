Flags
=====

Flags are atomic bitfields for tracking module state. They support optional compile-time-defined rules that are validated at runtime whenever a flag changes.

Defining Flags
--------------

Use ``FLAGS_DEFINE`` to declare a named bitfield along with any validation rules. Pass ``NO_FLAG_RULES`` if you don't need rules.

.. code-block:: c

   FLAGS_DEFINE(
       conn_flags,
       (CONN_OPEN, CONN_AUTHENTICATED, CONN_ERROR),
       FLAG_REQUIRES(CONN_AUTHENTICATED, CONN_OPEN)
       FLAG_EXCLUSIVE(CONN_OPEN, CONN_ERROR)
   );

This generates:
- an enum ``conn_flags_flags`` with the listed values
- an ``ATOMIC_DEFINE``'d bitfield named ``conn_flags``
- a validation function called on every set/clear (when ``CONFIG_FLAGS_VALIDATION=y``)

Setting and Checking Flags
--------------------------

.. code-block:: c

   SET_FLAG(conn_flags, CONN_OPEN);

   if (CHECK_FLAG(conn_flags, CONN_AUTHENTICATED)) {
       // ...
   }

   CLEAR_FLAG(conn_flags, CONN_OPEN);

All operations are atomic and thread-safe.

Validation Rules
----------------

Rules are checked after every ``SET_FLAG`` or ``CLEAR_FLAG`` when ``CONFIG_FLAGS_VALIDATION=y`` (default in ``DEBUG`` builds).

``FLAG_REQUIRES(A, B)``
   Asserts that if flag ``A`` is set, flag ``B`` must also be set.

``FLAG_EXCLUSIVE(A, B)``
   Asserts that flags ``A`` and ``B`` are never both set at the same time.

Rules run inline in the call that changed the flag. An assertion failure halts execution, making invalid flag states immediately visible during development.

Configuration
-------------

.. code-block:: kconfig

   CONFIG_GANTRY_FLAGS=y
   CONFIG_FLAGS_VALIDATION=y   # default on in DEBUG builds

API Reference
-------------

.. doxygendefine:: FLAGS_DEFINE

.. doxygendefine:: SET_FLAG

.. doxygendefine:: CLEAR_FLAG

.. doxygendefine:: CHECK_FLAG

.. doxygendefine:: FLAG_REQUIRES

.. doxygendefine:: FLAG_EXCLUSIVE
