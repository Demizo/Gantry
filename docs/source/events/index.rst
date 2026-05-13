Events
======

Events are the primary way modules communicate. Each event has a universal structure with a type field, so any module can inspect any event without knowing its origin. Event payloads are carried in a ``buffer_t`` embedded directly in the event.

Events are reference-counted memory blocks. The allocating module owns the initial reference. Any module that wants to retain the event past the current call frame must call ``EVENT_REF``, and must later call ``EVENT_UNREF`` to release it.

Defining Event Types
--------------------

Declare an event type in a header and define it in the corresponding source file. The ``_on_free`` callback is optional — use it to release any reference-counted data held in the event payload.

.. code-block:: c

   // my_module.h
   DECLARE_EVENT_TYPE(my_event);

   // my_module.c
   DEFINE_EVENT_TYPE(42, my_event, NULL);

Event IDs are checked for collisions at link time.

Usage
-----

Allocate an event with a payload size, fill in the payload, then pass or queue it.

.. code-block:: c

   event_t* ev = NULL;
   EVENT_ALLOC(&my_event, sizeof(struct my_payload), &ev);

   struct my_payload* p = (struct my_payload*)ev->data.buf;
   p->value = 123;

   // pass ev to another module; if they keep it:
   EVENT_REF(ev);

   EVENT_UNREF(&ev); // release our reference; sets ev = NULL

Linked Events
-------------

Events can be chained together via ``next_event``. Calling ``EVENT_REF`` or ``EVENT_UNREF`` on any event in the chain propagates to all linked events.

Releasing a Payload
-------------------

If the payload contains a reference-counted pointer, define an ``on_free`` callback to release it when the event is freed.

.. code-block:: c

   void my_on_free(event_t* event)
   {
       struct my_payload* p = (struct my_payload*)event->data.buf;
       MEM_UNREF(&p->data_block);
   }

   DEFINE_EVENT_TYPE(42, my_event, my_on_free);

API Reference
-------------

.. doxygendefine:: DECLARE_EVENT_TYPE

.. doxygendefine:: DEFINE_EVENT_TYPE

.. doxygenfunction:: event_alloc

.. doxygendefine:: EVENT_ALLOC

.. doxygenfunction:: event_ref

.. doxygenfunction:: event_unref

.. doxygenfunction:: event_init
