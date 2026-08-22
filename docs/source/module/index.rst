=======
Modules
=======

Overview
========

Gantry applications are composed of modules. Modules process events and communicate with each other via the :doc:`/stow/index`.

Modules are enabled and configured by Kconfig options. When enabled they run automatically on boot after the Stow has been initialized. This makes it trivial to include or exclude modules without extra boilerplate.

.. mermaid::

    %%{init: {"theme": "neutral"}}%%
    graph LR
        Stow[("Stow")]
        A["Module A"]
        B["Module B"]

        A -->|set| Stow
        B -->|get| Stow

Registering a Module
====================

Use :any:`GANTRY_MODULE_DEFINE` to register a module with its own dedicated thread, or :any:`GANTRY_LIBRARY_MODULE_DEFINE` for a module with init logic but no thread (e.g. one that only provides an API or configures another subsystem).

.. code-block:: c

   static void my_module_init(void);
   static void my_module_thread(void* arg1, void* arg2, void* arg3);

   GANTRY_MODULE_DEFINE(my_module, my_module_init, my_module_thread, CONFIG_MY_MODULE_STACK_SIZE, CONFIG_MY_MODULE_THREAD_PRIORITY);

   // A module with no thread of its own
   GANTRY_LIBRARY_MODULE_DEFINE(my_library, my_library_init);

Every registered module's ``init`` runs once at boot, after the :doc:`Stow </stow/index>` has been initialized. If the module has a thread, that thread is created immediately after ``init`` returns. 

.. mermaid::

    %%{init: {"theme": "neutral"}}%%
    graph TD
        B{"Boot"}
        K["Zephyr Kernel"]
        S["Stow"]
        M["Gantry Modules"]
        A("App's main function")

        B --> K
        K --> S
        S --> M
        M --> A

.. important::
   There is no ordering guarantee for module initialization; a module must not depend on another module having already started. Communicating through the Stow largely avoids cross-module dependencies. If dependencies are needed, consider adding SYS_INIT priorities or initializing the modules manually.

Philosophy
==========

Ideally, modules should only block in one place, their thread's event queue. This keeps a module's control flow easy to reason about. The samples contain examples of module structures. Note, a timeout can be added to the event queue to service a periodic event or state machine.

Modules communicate with each other through the :doc:`Stow </stow/index>`. Ideally, modules should not call directly into other modules unless they must be tightly coupled. Instead, a module that cares about another module's state subscribes to the relevant Stow item; a module that changes state sets it. This avoids the alternative where modules accumulate direct dependencies on each other.

Stow Subscriptions
==================

To receive updates from the Stow modules can use :any:`STOW_SUBSCRIPTION_DEFINE` to define static subscriptions that survive for the lifetime of the app. Alternatively, :any:`stow_subscribe` can be used for dynamic subscriptions at runtime.

Subscriptions require a callback to handle received update events. :any:`STOW_CALLBACK_DEFINE` can be used to define a simple callback that simply passes the event along to the module's event queue (if room is available). This is the recommended way to handle update events.

API Reference
=============

.. doxygengroup:: gantry_module
   :content-only:
