Memory Management
=================

Memory blocks are allocated from a set of fixed-size pools. Each allocation comes from the smallest pool that can satisfy the requested size. Blocks are reference-counted and freed automatically when the count reaches zero.

Configuration
-------------

Define up to 6 pools in ``prj.conf``. Block sizes must increase for each subsequent pool.

.. code-block:: kconfig

   CONFIG_GANTRY_MEMORY=y

   CONFIG_MEM_POOL_COUNT=3
   CONFIG_MEM_POOL1_BLOCK_SIZE=16
   CONFIG_MEM_POOL1_BLOCK_COUNT=128
   CONFIG_MEM_POOL2_BLOCK_SIZE=128
   CONFIG_MEM_POOL2_BLOCK_COUNT=64
   CONFIG_MEM_POOL3_BLOCK_SIZE=1024
   CONFIG_MEM_POOL3_BLOCK_COUNT=16

Usage
-----

Use the ``MEM_ALLOC``, ``MEM_REF``, and ``MEM_UNREF`` macros. These wrap the underlying functions and enable memory tracing when ``CONFIG_MEM_TRACE`` is set.

.. code-block:: c

   void* buf = NULL;
   MEM_ALLOC(64, &buf);  // allocate; ref count starts at 1

   MEM_REF(buf);         // share with another owner; ref count = 2

   MEM_UNREF(&buf);      // release; ref count = 1, buf unchanged
   MEM_UNREF(&buf);      // release; ref count = 0, buf freed and set to NULL

A newly allocated block starts at reference count 1. Each ``MEM_REF`` increments it; each ``MEM_UNREF`` decrements it. The pointer is set to ``NULL`` on free.

Watermarks
----------

Enable ``CONFIG_MEM_WATERMARK`` to receive a one-shot callback when a pool crosses a usage threshold.

.. code-block:: c

   void on_watermark(uint8_t pool_index, uint8_t percent)
   {
       LOG_WRN("Pool %d reached %d%% usage", pool_index, percent);
   }

   mem_set_watermark(0, 80, on_watermark);

The callback fires once. Call ``mem_set_watermark`` again to re-arm it.

Debugging
---------

``CONFIG_MEM_TRACE`` records the call site for each alloc, ref, and unref. Both options default to enabled in ``DEBUG`` builds.

.. code-block:: kconfig

   CONFIG_MEM_TRACE=y
   CONFIG_MEM_WATERMARK=y

API Reference
-------------

.. doxygengroup:: data_management
   :content-only:
