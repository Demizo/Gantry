================
Resource Checker
================

Overview
========

The resource checker finds resources that are acquired in a function and not released on every path to a ``return``. It covers Gantry's reference counted memory, events and Stow values, and common Zephyr resources such as IRQ locks, mutexes, spinlocks and ``net_buf``. It runs automatically when Gantry is included as a module.

The checker is a Python tool built on `tree-sitter <https://tree-sitter.github.io/tree-sitter/>`_. It reads the unexpanded source, so it needs no build, include paths or compile flags. It lives in ``tools/resource_check`` as the standalone ``resource-check`` package and does not depend on Gantry, so it can be installed into other projects.

Usage
=====

.. code-block:: sh

    uv run --project tools/resource_check resource-check <path>... [--annotations <file-or-dir>]...

Each ``<path>`` is a ``.c`` file or a directory searched for ``.c`` files. The checker exits non-zero if it reports a leak, a policy violation, an annotation error or code it could not parse.

Annotations are used to determine what function acquire or release resources. The resource checker includes annotation for common Zephyr functions that allocate or release resources. Annotations are read from functions in the checked source, ``--annotations`` provides additional files to read annotations from.

Annotations
===========

What a function acquires or releases is declared with tags in the doc comment directly above its declaration, definition or ``#define``:

.. code-block:: c

    /** @acquires memory block_ptr on-success */
    int mem_alloc(size_t size, void** block_ptr);

    /** @releases memory block_ptr */
    void mem_unref(void** block_ptr);

``@acquires <class> <target> [on-success | on-non-null]``
    Calling the function acquires a resource of the given class.

``@releases <class | *> <target>``
    Calling the function releases a resource. The class ``*`` releases the identifier whatever class it was acquired as.

The class is a name for the kind of resource, and a release only matches an acquire of the same class. The target is a parameter name, or ``return`` for the variable that receives the result.

Most allocations only acquire when they succeed. ``on-success`` means success is a return value of ``0`` (``SUCCESS``), and ``on-non-null`` means a non-NULL return value. On the failure branch of a test of the result nothing is held, so returning there is not a leak:

.. code-block:: c

    void* buf = NULL;
    if (MEM_ALLOC(16, &buf) != SUCCESS)
    {
        return -ENOMEM; // fine, nothing was allocated
    }
    MEM_UNREF(&buf);

Ownership Transfer
==================

When ownership of a resource leaves a function, mark the point with :any:`PASS_OWNERSHIP`. The checker does not infer a transfer from a value being stored in a field, returned, or passed to another function.

.. code-block:: c

    int custom_alloc(void** block)
    {
      void* new_block = NULL;
      int ret = mem_alloc(42, &new_block);
      if (ret != SUCCESS)
      {
         return ret;
      }

      *block = new_block;
      PASS_OWNERSHIP(new_block);
      return SUCCESS;
    }

Custom Resources
================

Applications describe their own resources by annotating their functions in place. Annotating ``custom_alloc`` above in its header lets the checker require that callers release the block:

.. code-block:: c

    /** @acquires memory block on-success */
    int custom_alloc(void** block);

A new kind of resource only needs a new class name, for example ``@acquires lease handle on-success`` with a matching ``@releases lease handle``.

Limitations
===========

The checker works on one function at a time and does not follow a value copied to a second variable. It only reports leaks, not issues like double-frees, which the memory API protects against. See :ref:`mem-leak-detection` for the memory rules.
