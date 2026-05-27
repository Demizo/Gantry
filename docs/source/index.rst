======
Gantry
======

Gantry is a framework made to bootstrap embedded applications. The architecture is event-driven and data-centric. Applications are made up of composable modules that communicate with each other via events. Modules can define custom event types and payloads, but the core event structure is universal such that any module can parse any event.

The system state, configuration, and device information is all managed by a central data storage module, the Stow. Modules, as well as connected devices, can subscribe to, set, and get items in the Stow. This data-centric approach makes it trivial for modules or devices to share state or react to events. It avoids common pitfalls where many modules define similar events or pass state via arbitrary event payloads. It also prevents the need for bloated protocols for interacting with or querying information from devices.

.. toctree::
   :maxdepth: 1
   :caption: Features

   Stow <stow/index>
   Memory Management <memory_management/index>
   Resource Checker <resource_checker/index>
   Events <events/index>
   Flags <flags/index>


Integration Quick Start
=======================

The project is meant to be included as a West module within a West workspace.

The ``samples`` folder provides an example application using Gantry.

1. Include the module's repository in your West manifest then run ``west update``.
2. Define a ``stow.yaml`` in your applications root directory, see :doc:`/stow/schema`.
3. Enable the desired Gantry features in your project's ``prj.conf``.
4. Build our application and begin making use of the Gantry library.

License
=======

Gantry is licensed under the Apache 2.0 license, see the ``LICENSE`` file for the full license text.