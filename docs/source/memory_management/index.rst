=================
Memory Management
=================

Overview
========

Gantry's memory model is based around referenced counted blocks of memory. Memory blocks are allocated from a set of fixed-size pools. Each allocation comes from the smallest pool that can satisfy the requested size. Blocks are reference-counted and freed automatically when the count reaches zero.

Usage
=====

Memory can be allocated using :any:`MEM_ALLOC`. A pointer must be provided that will be populated with a block pointer assuming one is available. The provided pointer must be ``NULL`` to prevent the caller from accidentally overwriting data.

When successful, the caller is provided a block with a reference count of one. To increment the reference count use :any:`MEM_REF`. Likewise, :any:`MEM_UNREF` decrements the reference count. Once the reference count reaches zero, the block is freed and available for future allocations. When a block is freed, the pointer provided by the caller is set to ``NULL``. It is okay to call :any:`MEM_UNREF` on a block that has already been freed (the provided block pointer points to ``NULL``).

.. code-block:: c

   void* buf = NULL;
   MEM_ALLOC(64, &buf);  // allocate; ref count starts at 1

   MEM_REF(buf);         // share with another owner; ref count = 2

   MEM_UNREF(&buf);      // release; ref count = 1, buf unchanged
   MEM_UNREF(&buf);      // release; ref count = 0, buf freed and set to NULL

While the direct functions like :any:`mem_alloc` can be used, the macros are recommended as they allow for tracing the source of memory operations when memory tracing is enabled. If you are writing a wrapper around the memory manager, you may want to use the direct function calls instead. See :ref:`mem-tracing` for more information.

.. _mem-leak-detection:

Memory Leak Detection
---------------------

To avoid memory leaks, the general memory model is as follows: **the function that allocates memory should free it**. If the memory block is passed to another function, as is often the case, that function should reference count the memory to claim ownership, then free it when done.

This model allows the :doc:`Resource Checker </resource_checker/index>` to automatically detect memory leaks. It also keeps memory management consistent and clear. If a function allocates or references memory, it must also free it.

There will be exceptions to this rule. For example, when making a helper function that allocates memory and provides it to the caller. In these cases, the :any:`PASS_OWNERSHIP` macro can be used to indicate to the resource checker that the memory is intentionally not freed. Additionally, when exiting a function early due to a failed allocation, the resource checker must be informed that the memory was not allocated and therefore does not need to be freed. For this, use the :any:`NOT_REFERENCED` macro. 

.. code-block:: c

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

.. note::
   You **must not** provide pointers as complex expressions to the memory API. This allows the :doc:`Resource Checker </resource_checker/index>` to identify memory leaks. The resource checker will warn you when complex expressions are used.

   .. code-block:: c

      // Do this
      void* buf = buffers[i];
      MEM_REF(buf);

      // NOT this
      MEM_REF(buffers[i]); // The resource checker will warn you

The :doc:`Resource Checker </resource_checker/index>` is not a substitute for proper memory management. It will catch common cases within a single function scope, but it will not detect more complex leaks. Regardless, it should provide a safety net and some peace of mind. Additionally, if you create helper functions that allocate memory, you can add them to the resource checker, see :doc:`Resource Checker </resource_checker/index>` for more information.

Configuration
=============

The memory module allows you to define up to six pools of memory blocks. You can set the number of pools as well as their block size and count via the following Kconfig options:

.. code-block:: kconfig

   CONFIG_GANTRY_MEMORY=y

   CONFIG_MEM_POOL_COUNT=3
   CONFIG_MEM_POOL1_BLOCK_SIZE=16
   CONFIG_MEM_POOL1_BLOCK_COUNT=128
   CONFIG_MEM_POOL2_BLOCK_SIZE=128
   CONFIG_MEM_POOL2_BLOCK_COUNT=64
   CONFIG_MEM_POOL3_BLOCK_SIZE=1024
   CONFIG_MEM_POOL3_BLOCK_COUNT=16

.. important::
   Block sizes **must** increase as the pool number rises. This ensures the memory manager can allocate using the smallest pool possible.


.. _debugging:

Debugging
=========

Debugging memory issues is no fun, so Gantry provides some tools to help!

.. _mem-tracing:

Memory Tracing
--------------

When memory tracing is enabled via ``MEM_TRACE``, each memory block tracks which function originally allocated it, the caller of the ``MEM_ALLOC`` macro. 

If you are writing a helper function to wrap memory allocation, you might want to track who called your function as the original creator of the memory. In this case, use the `mem_*` functions directly in your helper and wrap calls to your helper in the :any:`TRACE_WRAP` (or :any:`TRACE_WRAP_VOID` for void return values) macro. This will result in the caller of your API being considered the original allocator of the block.

.. code-block:: c

   int alloc_helper(void** block) {
      return mem_alloc(42, block);
   }

   void foo() {
      void* block = NULL;
      TRACE_WRAP(alloc_helper(&block));
      // `foo` is tracked as the allocator of `block`
   }

This same tracing logic applies to referencing and dereferencing memory blocks. To view this trace information raise the ``MEM_LOG_LEVEL`` to Debug. This will log all memory operations including: what operation is occurring, what function triggered the operation, and what function allocated the block being operated on.

Watermarks
----------


To keep your memory usage in check, and potentially identify leaks, you can use watermarks. Enable watermarks using the ``MEM_WATERMARK`` Kconfig option. When enabled you can register a one-shot callback to fire when a pool crosses a usage threshold. The callback fires once; call :any:`mem_set_watermark` again to re-arm it.

.. code-block:: c

   void on_watermark(uint8_t pool_index, uint8_t percent)
   {
       LOG_WRN("Pool %d reached %d%% usage", pool_index, percent);
   }

   // Fire `on_watermark` when the first pool (0-indexed) reaches 80 percent
   mem_set_watermark(0, 80, on_watermark);

Additionally, you can manually check the current usage of each pool using :any:`mem_get_pool_usage`. This is available regardless of whether watermarks are enabled.

API Reference
=============

.. doxygengroup:: memory
   :content-only:
