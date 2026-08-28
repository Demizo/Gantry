====
Stow
====


The Stow is the central data store for a Gantry application. It contains system state, configuration values, and device information in the form of individual Stow items (e.g. a string for device name, an integer for battery level, or an enumeration for connection state). The Stow acts as a single source of truth.

Firmware modules and external clients can set, get, or subscribe to items in the Stow. In this way, the Stow acts as a universal interface for device interactions. It provides both a direct API for firmware modules as well as the :doc:`/stow/protocol` for external client communications.

By taking a data-driven approach to application architecture, most device functionality, information, and configuration can be clearly represented as a simple collection of data items. The Stow supports a handful of core primitive item types and allows the users to define custom struct types to fit their application's needs. Stow items are defined by the application's ``stow.yaml`` :doc:`schema </stow/schema>` file. The underlying C code is automatically generated at compile time.

.. toctree::
   :maxdepth: 1

   Types <types>
   Schema <schema>
   API <api>
   Protocol <protocol>

How It Works
============

The Stow is a key-value store built for firmware. It's built to hold an application's information, state, and configuration. Instead of scattered state variables, callbacks, and APIs, the Stow acts as a single source of truth.

.. mermaid::

   %%{init: {"theme": "neutral"}}%%
   flowchart LR
       Info([Device Info]) --> Stow[(Stow)]
       State([State]) --> Stow
       Config([Configuration]) --> Stow

Stow Items
----------

The Stow represents everything as items. These items are defined by a ``stow.yaml`` file. When the application is built, the Stow implementation is automatically generated. This makes it trivial to add or modify items.

.. code-block:: yaml

  - name: DeviceName
    description: "The name of the device"
    categories: [System]
    type: STRING
    storage: PERSISTENT
    permissions:
      - read: ANY
      - write: [User]
    default: "Gantry"
    constraints:
      - min_len: 3
      - max_len: 25

Each item also has a datatype, storage mode, and constraints. The storage mode is how an item gets stored (e.g. does it persistent across reboots?). The constraints are type-specific limits on acceptable values. The Stow provides a handful of common primitive types, and custom types can also be defined.

Stow Interactions
-----------------

Directly sharing state between modules can often create tight coupling. For example, an application may require that new device connections light up an LED indicator. These connection events may also need to be logged. In a tightly coupled system, a connection manager may directly call into the LED indicator and logger, tangling logic and dependencies. Even in an event-driven system, modules still need to agree on event definitions and payloads.

.. mermaid::
   
   %%{init: {"theme": "neutral"}}%%
   flowchart LR
    subgraph Coupled Architecture
        A[Connection Manager] -->|Direct Call| B[LED Indicator]
        A -->|Direct Call| C[Logger]
    end

The Stow addresses this by allowing modules to interact with each other indirectly via Stow items. Modules can **set**, **get**, and **subscribe** to items in the Stow. For example, our connection manager would update a connection state item within the Stow. The LED indicator and logger could simply subscribe to the connection status and react accordingly.

.. mermaid::

   %%{init: {"theme": "neutral"}}%%
   graph LR
    subgraph Gantry Architecture
       CM["Connection Manager"] -->|Update| Stow[("Stow")]
       Stow --> |Notify| LED["LED Manager"]
       Stow --> |Notify| Logger["Logger"]
    end

External Interactions
---------------------

These interactions are not limited to internal modules. External clients can also **set**, **get**, and **subscribe** to items in the Stow via the Stow Protocol. The protocol is transport independent and can be easily wrapped inside arbitrary higher-level protocols.

Notably, the Stow is self describing. External clients can read the Stow's description to discover items. This means external clients can dynamically discover device information and capabilities. For example, a mobile app could support multiple different devices or versions without hardcoding device or version specific details.

The Stow Protocol's simple set of interactions can support a myriad of features. For example, a client may write to an item to set the device name or subscribe to battery status for live updates.

.. mermaid::

   %%{init: {"theme": "neutral"}}%%
   graph LR
    C["External Client"]
    subgraph Stow
       N("Device Name")
       B("Battery Status")
    end
       C -->|Set| N
       C -->|Subscribe| B
       B --> |Notify| C

Permissions
-----------

The Stow Protocol provides powerful controls to external clients, but great power comes with great responsibility. Luckily, the Stow provides role-based permissions out of the box. Each item defines which roles have read and/or write authority (if any). All external interactions are validated against an item's permissions and value constraints.

Benefits
--------

- **Single source of truth**: Information, state, and configuration live in one place.
- **Less boilerplate**: A schema file defines everything. The code is generated automatically with type checking and value bounds checks.
- **Extendable**: Firmware modules can be added, swapped, or removed without modifying existing code. 
- **Versatile**: Stow items can represent anything. External clients can discover functionality and interact with devices using a simple stable protocol.
