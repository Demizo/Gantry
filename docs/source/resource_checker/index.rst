================
Resource Checker
================

Overview
========

The resource checker reports a resource acquired in a function and not released on some path to a ``return``, a resource released a second time, and a resource used after its release. It covers Gantry's reference counted memory, events and Stow values, and common Zephyr resources such as IRQ locks, mutexes, spinlocks and ``net_buf``. It runs automatically when Gantry is included as a module.

The checker is a Python tool built on `tree-sitter <https://tree-sitter.github.io/tree-sitter/>`_. It reads the unexpanded source, so it needs no build, include paths or compile flags. It lives in ``tools/resource_check`` as the standalone ``resource-check`` package and does not depend on Gantry, so it can be installed into other projects.

Usage
=====

.. code-block:: sh

    uv run --project tools/resource_check resource-check <path>... [--annotations <file-or-dir>]...

Each ``<path>`` is a ``.c`` file or a directory searched for ``.c`` files. The checker exits non-zero if it reports a leak, a second release, a use after release, a policy violation, an annotation error or code it could not parse.

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

A function that takes ownership of an argument carries ``@releases`` on that parameter, so its callers need no marker. When ownership leaves a function any other way, through a field, an out parameter or a queue, mark the point with :any:`PASS_OWNERSHIP`. The checker does not infer a transfer from a value being stored in a field, returned, or passed to an unannotated function.

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

Obligations
===========

A resource class need not be memory. An obligation is something that must happen exactly once, and the checker enforces it the same way. The Stow client's ``slot`` class makes every request complete exactly once:

.. code-block:: c

    /** @acquires slot return on-non-null */
    struct slot* z_stow_client_slot_find_sent(struct stow_client_peer* peer, uint8_t tag);

    /** @releases slot slot */
    void z_stow_client_slot_finish(struct stow_client_peer* peer, struct slot* slot, int err, uint16_t index);

A response handler that finds a slot and returns without finishing it is reported, and so is one that finishes it twice.

Two rules keep code checkable:

- **Obligations live in locals.** A resource held in a struct field is not tracked, so code that holds one across a function keeps it in a local and passes the local.
- **Resource calls go through named functions.** A call through a function pointer is not tracked, so wrap it in a named inline function that carries the annotations, such as ``stow_transport_send`` for a transport's ``send``.

Limitations
===========

The checker works on one function at a time. It does not track:

- uses of a released identifier other than passing the identifier, its address or a cast of it to a call
- a value copied to a second variable
- resources held in struct fields
- calls through function pointers

An identifier the function never acquired, such as a parameter or a local copied from a field, may be released once.

A second release and a use after release are only reported when every path to the call has already released the identifier. A release in a loop of something acquired before the loop is not reported.

An identifier holds one reference. Two references held in the same identifier and released twice are reported as a second release; hold the second reference in its own variable.

See :ref:`mem-leak-detection` for the memory rules.
