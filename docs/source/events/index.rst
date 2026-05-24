======
Events
======

Overview
========

Events are the primary way modules communicate. Each event has a universal structure with a type field, so any module can inspect any event without knowing its origin.

The payload of an event is a :any:`buffer_t` of arbitrary size. The structure of the buffer data is defined by the event type. Gantry comes with built in events, but applications and other modules are free to defined additional event types, see :ref:`define-event`.

Usage
=====

Events are allocated in memory blocks. Events can be referenced counted. They are freed when the reference count reaches zero.

.. code-block:: c

   event_t* event = NULL;
   EVENT_ALLOC(&my_event_type, sizeof(struct my_payload), &event);

   struct my_payload* payload = (struct my_payload*)event->data.buf;
   payload->value = 123;

   EVENT_REF(event); // Increment reference count
   EVENT_UNREF(&event); // Decrement reference count
   EVENT_UNREF(&event); // Free

Events follow the same memory management rules that apply to memory blocks, see :ref:`mem-leak-detection`.

Linking Events
==============

There may be times where multiple events belong together. Events contain a ``next_event`` field such that events can be chained together. Reference counting an event also applies the operation to events in the chain.

.. _define-event:

Defining Events
===============

To create you own event, declare an event type in a header and define it in the corresponding source file. Event IDs are automatically checked for collisions at link time.

.. code-block:: c

   // my_module.h
   #define EVENT_ID_MY_EVENT 42

   DECLARE_EVENT_TYPE(my_event_type);

   // my_module.c
   DEFINE_EVENT_TYPE(EVENT_ID_MY_EVENT, my_event_type, NULL);

Releasing a Payload
-------------------

If the payload contains a reference-counted data, define an ``on_free`` callback to release it when the event is freed. ``on_free`` is called when an event is about to be freed. This allows freed events to automatically dereference owned data.

.. code-block:: c

   void my_on_free(event_t* event)
   {
       struct my_payload* payload = (struct my_payload*)event->data.buf;
       MEM_UNREF(&payload->data_block);
   }

   DEFINE_EVENT_TYPE(42, my_event_type, my_on_free);

API Reference
=============

.. doxygengroup:: events
   :content-only:

.. doxygengroup:: buffer
   :content-only: