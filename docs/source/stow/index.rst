====
Stow
====


The Stow is the central data store for a Gantry application. It contains system state, configuration values, and device information in the form of individual Stow items. Items can be various types (e.g. a string for device name, an integer for battery level, or an enumeration for connection state). Firmware modules and external devices can set, get, or subscribe to items in the Stow. In this way, the Stow acts as a universal interface for device interactions. It provides both a direct API for firmware modules as well as the Stow Protocol for external device communications.

By taking a data-driven approach to application architecture, most device functionality, information, and configuration can be clearly represented as a simple collection of data items. The Stow supports a handful of core primitive item types and allows the users to define custom struct types to fit their application's needs. Stow items are defined by the application's ``stow.yaml`` :doc:`schema </stow/schema>` file. The underlying C code is automatically generated at compile time.

.. toctree::
   :maxdepth: 1

   Types <types>
   Schema <schema>
   API <api>
   Protocol <protocol>

Use Cases
=========

Internal State
--------------

The Stow makes it trivial for firmware modules to share device state. For example, the Stow may contain an item for BLE Connection State. A BLE manager sets the connection state item. Other modules can subscribe to the item to be notified when it changes (e.g. an LED manager which changes an indicator light). 

Want to add logging when important values change? Simply create a logging module that subscribes to those items. Logging logic can remain independent from individual modules.

.. mermaid::

    %%{init: {"theme": "neutral"}}%%
    graph LR
        %% Internal Modules
        BLE["BLE Manager"] -->|set| Stow
        LED["LED Manager"] -->|subscribe| Stow
        Logger["Logger"] -->|subscribe| Stow

        %% The Central Core with Items Inside
        Stow[("Stow Database<br>━━━━━━━<br>▪ String Item<br>▪ Integer Item<br>▪ Enum Item")]

External Communication
----------------------

The Stow can contain arbitrary items. By implementing the Stow Protocol, external clients can simply discover device information and state similar to BLE GATT discovery. Unlike BLE GATT, the Stow is not tied to any particular communication medium.

As an example, external clients can read information from Stow items (e.g. device name and serial number). Values can be modified to change the device's configuration. The external client can subscribe to values to be notified when they change (e.g. battery level or a sensor data stream).

.. mermaid::

    %%{init: {"theme": "neutral"}}%%
    graph LR
        %% The Central Core with Items Inside
        Stow[("Stow Database<br>━━━━━━━<br>▪ Device Name<br>▪ Serial Number<br>▪ Battery Level")]

        %% External Interface
        Ext["External Client"] <-->|Set, get, & subscribe via the Stow Protocol| Stow


The handful of Stow Protocol commands allow for most device interactions (e.g. setting and getting values). This avoids ballooning protocols with numerous getter and setter commands.

