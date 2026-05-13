Gantry documentation
====================

Gantry is a framework made to bootstrap embedded applications. The architecture is event-driven and data-centric. Applications are made up of composable modules that communicate with each other via events. Modules can define custom event types and payloads, but the core event structure is universal such that any module can parse any event.

The system state, configuration, and device information is all managed by a central data storage module, the Stow. Modules, as well as connected devices, can subscribe to, set, and get items in the Stow. This data-centric approach makes it trivial for modules or devices to react to events and communicate with each other. It avoids common pitfalls where many modules define similar events or pass state via arbitrary event payloads.

.. toctree::
   :maxdepth: 2
   :caption: Contents:

   Memory Management <memory_management/index>
   Events <events/index>
   Flags <flags/index>
   Stow <stow/index>
