=====
Flags
=====

While the :doc:`Stow </stow/index>` can be used to share state across modules. Individual modules often need to keep track of local state. In many cases, local state can be boiled down to a series of state flags. Gantry provides tooling to easily create and validate module state flags. 

Flags are represented as atomic bitfields used for tracking module state. They support optional compile-time-defined rules that are validated at runtime whenever a flag changes.

Defining Flags
==============

Use ``FLAGS_DEFINE`` to declare a named bitfield along with any validation rules. Pass ``NO_FLAG_RULES`` if you don't need rules.

.. code-block:: c

   FLAGS_DEFINE(
       conn_flags,
       (CONN_OPEN, CONN_AUTHENTICATED, CONN_ERROR),
       FLAG_REQUIRES(CONN_AUTHENTICATED, CONN_OPEN)
       FLAG_EXCLUSIVE(CONN_OPEN, CONN_ERROR)
   );

In this example, ``CONN_AUTHENTICATED`` is only valid if ``CONN_OPEN`` is set. ``CONN_OPEN`` and ``CONN_ERROR`` cannot both be set at the same time.

Setting and Checking Flags
==========================

.. code-block:: c

   SET_FLAG(conn_flags, CONN_OPEN);

   if (CHECK_FLAG(conn_flags, CONN_AUTHENTICATED)) {
       // ...
   }

   CLEAR_FLAG(conn_flags, CONN_OPEN);

All operations are atomic and thread-safe.

Validation Rules
================

Rules are checked after every ``SET_FLAG`` or ``CLEAR_FLAG`` when runtime validation is enabled.

``FLAG_REQUIRES(A, B)``
   Asserts that if flag ``A`` is set, flag ``B`` must also be set.

``FLAG_EXCLUSIVE(A, B)``
   Asserts that flags ``A`` and ``B`` are never both set at the same time.

When ``CONFIG_FLAGS_VALIDATION=y``, validation rules run inline in the call that changed the flag. An assertion failure halts execution, making invalid flag states immediately visible during development.

Configuration
=============

.. code-block:: kconfig

   CONFIG_GANTRY_FLAGS=y
   CONFIG_FLAGS_VALIDATION=y # defaults to on in DEBUG builds

API Reference
=============

.. doxygendefine:: FLAGS_DEFINE

.. doxygendefine:: SET_FLAG

.. doxygendefine:: CLEAR_FLAG

.. doxygendefine:: CHECK_FLAG

.. doxygendefine:: FLAG_REQUIRES

.. doxygendefine:: FLAG_EXCLUSIVE
