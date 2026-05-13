Stow Schema (stow.yaml)
=======================

The Stow schema is defined in a ``stow.yaml`` file inside your application. The build system runs the code generator during the build to produce the typed C stubs for all items.

To wire up code generation in your ``CMakeLists.txt``:

.. code-block:: cmake

   gantry_stow_generate(YAML ${CMAKE_CURRENT_SOURCE_DIR}/stow.yaml)

This generates ``generated_stow_items.h/.c``, ``generated_stow_enums.h/.c``, and struct-specific files under ``${CMAKE_CURRENT_BINARY_DIR}/gantry_generated/``.

File Structure
--------------

A ``stow.yaml`` file has four top-level sections: ``enums``, ``structs``, ``categories``, and ``items``.

Enums
-----

Enums define named integer values. Stow items of type ``ENUM`` reference one of these.

.. code-block:: yaml

   enums:
     BleConnectionState:
       description: "Bluetooth connection state"
       values:
         - name: DISCONNECTED
           value: 0
         - name: CONNECTED
           value: 1

Structs
-------

Structs are composite types with named fields. Fields can be any Stow type, including nested structs.

.. code-block:: yaml

   structs:
     - name: SpiBuffer
       description: "SPI buffer with chip select and data"
       fields:
         - name: CS
           type: ENUM
           constraints:
             - enum: SpiChipSelect
         - name: Bytes
           type: BYTE_ARRAY
           constraints:
             - min_len: 0
             - max_len: 6
         - name: Text
           type: STRING
           constraints:
             - min_len: 3
             - max_len: 12

Categories
----------

Categories are plain strings used to group related items in client UIs. Each item must belong to at least one category.

.. code-block:: yaml

   categories:
     - System
     - BLE
     - SPI

Items
-----

Items are the individual data entries in the Stow. Each item has a type, storage mode, permissions, default value, and constraints.

.. code-block:: yaml

   items:
     - name: DeviceName
       description: "Human-readable device name"
       categories: [System]
       type: STRING
       storage: PERSISTENT
       permissions:
         - read: ANY
         - write: SESSION
       default: "Gantry"
       constraints:
         - min_len: 3
         - max_len: 25

     - name: SerialNumber
       description: "Device serial number, write-once"
       categories: [System]
       type: STRING
       storage: TOFU
       permissions:
         - read: ANY
         - write: SESSION
       default: "0000000000000000"
       constraints:
         - min_len: 16
         - max_len: 16

     - name: BleConnectionState
       categories: [System, BLE]
       type: ENUM
       storage: EPHEMERAL
       permissions:
         - read: ANY
         - write: INTERNAL
       default: DISCONNECTED
       constraints:
         - enum: BleConnectionState

Constraints by Type
-------------------

**INT / FLOAT**

.. code-block:: yaml

   constraints:
     - min: -100
     - max: 100

**STRING / BYTE_ARRAY / BUFFER**

.. code-block:: yaml

   constraints:
     - min_len: 0
     - max_len: 64

**ENUM**

.. code-block:: yaml

   constraints:
     - enum: MyEnumName

**STRUCT**

.. code-block:: yaml

   constraints:
     - struct: MyStructName

Permissions
-----------

Both ``read`` and ``write`` take one of: ``ANY``, ``SESSION``, ``DEV``, ``INTERNAL``, ``NONE``.

Generated Code
--------------

After generation, items are accessed via the ``STOW_ID_<NAME>`` enum (e.g. ``STOW_ID_DEVICE_NAME``). All IDs are defined in ``generated_stow_items.h``.
