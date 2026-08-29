======
Gantry
======

Gantry is a Zephyr module for building connected embedded applications. The architecture is event-driven and data-centric. It makes applications composable, reusable, and trivial to extend.

Overview
========

A Gantry application is composed of independent :doc:`/module/index`. Each module communicates with the rest of the system through :doc:`/events/index`. State is shared between modules via the :doc:`/stow/index`.

The Stow is the core of a Gantry application. It is a key-value store with device configuration, live state, and information. The Stow uses a publish/subscribe model where modules can update values and get notified about changes. External clients can access the Stow via the :doc:`/stow/protocol`. The Stow is self-describing so clients can dynamically discover information and supported features. The protocol includes role based authentication, type validation, and value constraints for all items within the Stow. The protocol's basic operations (set, get, and subscribe) eliminate the need for overly complex communication protocols.

Benefits
--------

- Modules are independent, composable, and reusable.
- Values can be easily shared between modules and exposed to external clients.
- Application items, permissions, and constraints can be arbitrarily declared in a schema, not reimplemented per feature.
- Reference-counted memory is paired with a static resource checker to catch leaks at build time.

.. toctree::
   :maxdepth: 1
   :caption: Features

   Stow <stow/index>
   Memory Management <memory_management/index>
   Resource Checker <resource_checker/index>
   Events <events/index>
   Modules <module/index>
   Flags <flags/index>
   COBS Framer <cobs_framer/index>


Integration Quick Start
=======================

The project is meant to be included as a West module within a West workspace.

The ``samples`` folder provides an example application using Gantry.

1. Include the module's repository in your West manifest then run ``west update``.

   .. code-block:: yaml

      manifest:
        remotes:
          - name: gantry
            url-base: https://github.com/Demizo

        projects:
          - name: gantry
            remote: gantry
            repo-path: Gantry
            revision: main
            path: modules/lib/gantry

2. Define a ``stow.yaml`` in your application's root directory, see :doc:`/stow/schema`.
3. Enable the desired Gantry features in your project's ``prj.conf``.
4. Build your application and begin making use of the Gantry library.

License
=======

Gantry is licensed under the Apache 2.0 license, see the ``LICENSE`` file for the full license text.
