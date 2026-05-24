================
Resource Checker
================

Overview
========

The resource checker allows applications to ensure they are properly cleaning up or dereferencing resources to avoid leaks. The resource checker currently detects leaks of reference counted memory and dangling IRQ locks.

Usage
=====

The resource checker will always run against the application when Gantry is included as a module. This will check your app for proper usage of memory allocation and referencing/dereferencing.

Limitations
-----------

The resource checker relies on the ``spatch`` command provided by `Coccinelle <https://www.kernel.org/doc/html/v4.15/dev-tools/coccinelle.html>`_. Analysis will be skipped when Coccinelle is not available. Coccinelle is only officially provided for Linux and MacOS, a container may be required to run analysis on Windows.

The resource checker only detects memory leaks within the scope of a single function. See :ref:`mem-leak-detection` for the rules around memory management. The resource checker currently only detects basic violations (something was allocated or referenced but not freed). It does not detect issues like double-frees. Note that the memory API itself protects against this.

Defining Custom Resources
-------------------------

If the application creates additional functions that allocate or reference memory, it can define a ``resource_functions.yaml`` file at the root of the application directory. The resource checker will automatically run analysis against the functions defined in the ``resource_functions.yaml``. For example:

.. code-block:: c

    // Custom allocation function
    int custom_alloc(void** block)
    {
      void* new_block = NULL;
      int ret = mem_alloc(42, &new_block);
      if (ret != SUCCESS)
      {
         // Alloc didn't succeed so there is nothing to free
         NOT_REFERENCED(new_block);
         return ret;
      }

      *block = new_block;

      // The intent is to give ownership to the caller
      PASS_OWNERSHIP(new_block);
      return SUCCESS;
    }

``custom_alloc`` would be defined as the following function resource:

.. code-block:: yaml

    alloc_functions:
    - name: custom_alloc
        single_param: false

This registers ``custom_alloc`` as a function that allocates memory and takes in a single parameter (the allocated block). If ``custom_alloc`` is used, the resource checker will automatically ensure that the memory is freed.

The ``resource_functions.yaml`` has the following options: ``alloc_functions`` for functions that allocate or reference memory and ``dealloc_functions`` for functions that free or dereference memory. 

For each function, ``single_param`` must be set to ``true`` or ``false``. When set to ``true``, the resource checker knows that the first and only argument is the memory. When ``false``, the resource checker treats the **last** argument as the allocated memory. The following is a snippet from Gantry's built-in ``resource_functions.yaml``:

.. code-block:: yaml


    alloc_functions:
    - name: MEM_ALLOC
        single_param: false
    - name: MEM_REF
        single_param: true
    ...

    dealloc_functions:
    - name: MEM_UNREF
        single_param: true
    ...





