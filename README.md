# Gantry

Gantry is a framework for bootstrapping embedded applications using the [Zephyr RTOS](https://zephyrproject.org/). The architecture is event-driven and data-centric. Applications are made up of composable modules that communicate with each other via events. Modules can define custom event types and payloads, but the core event structure is universal such that any module can parse any event.

The system state, configuration, and device information is all managed by a central data storage module, the Stow. Modules, as well as connected devices, can subscribe to, set, and get items in the Stow. This data-centric approach makes it trivial for modules or devices to react to events and communicate with each other. It avoids common pitfalls where many modules define similar events or pass state via arbitrary event payloads.

**What's included:**

| Component | Description |
| --- | --- |
| Stow | Central data store for device information, state, and configuration. Modules and external clients can set, get, and subscribe to items in the Stow. |
| Memory Management | Configurable reference-counted memory pools. |
| Events | Universal event format for inter-process communications. |
| Resource Checker | Static analysis for memory and resource leaks. |
| Flags | State flags for managing and monitoring module state. |

## Integration Quick Start

The project is meant to be included as a West module within a West workspace.

1. Include the module's repository in your West manifest then run ``west update``.
2. Define a ``stow.yaml`` in your applications root directory.
3. Enable the desired Gantry features in your project's ``prj.conf``.
4. Build our application and begin making use of the Gantry library.

The ``samples`` folder provides an example application using the Gantry library.

## Project Structure

The project is meant to be included as a West module within a West workspace.

- ``docs`` contains project documentation.
- ``include`` and ``lib`` contain the headers and source for the Gantry library, respectively.
- ``samples`` contains example applications using the Gantry library.
- ``tests`` contains a suite of unit tests.
- ``tools`` contains useful Python utilities, code generation, and analysis tools.
- ``python`` contains an example implementation of a Stow client.

## Setup

### Prerequisites

- [Nix](https://nixos.org/download) with flakes enabled
- If you prefer not to use Nix, review the dependencies within the `shell.nix` file and install them on your system.
- Additionally, a Docker container of the environment can be built using `just docker-build`. This container can be leveraged to use the development environment on systems without the necessary dependencies.

### Enter the development shell

```sh
nix develop
```

> Alternatively, with `direnv` installed, run `direnv allow` to automatically enabled the environment when entering the project folder.

This project uses `just` to easily run commands and scripts. Run `just` to view available commands.

## Documentation

Generate and open the HTML docs:

```sh
# Build the documentation
just docs
# Open the library documentation
just docs-open
# Open the source code documentation
just doxy-open
```

### Initialize the Workspace

```sh
just init
```

To re-sync after the West manifest changes run:

```sh
just update-workspace
```

## Building and Flashing

Build the example application for the nRF52840 DK. Gantry does not have specific hardware requirements so most 32-bit targets should work:

```sh
just build
```

For a pristine rebuild:

```sh
just rebuild
```

Flash to a connected board via OpenOCD:

```sh
just flash
```

Mass-erase the board:

```sh
just erase
```

## Running Unit Tests

Run all unit tests on the native simulator:

```sh
just test-all
```

Run tests for a specific component:

```sh
just test stow
just test memory
...
```

## License

Gantry is licensed under the Apache 2.0 license, see the ``LICENSE`` file for the full license text.
