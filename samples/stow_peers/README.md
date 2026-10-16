# Stow Peers

A controller and a sensor over one UART link, using the interim raw COBS transport. Each is a Stow server for its own items and a Stow client of the other's.

- The `sensor` drains its `Battery` by 1% every second, starting over at 100% once empty, and has a `Mode` setting. When the controller connects, the sensor reads its `Name`.
- The `controller` subscribes to the sensor's `Battery`, then reads it, so no change in between is missed. It puts the sensor in `ECO` mode when the battery drops below 20%.

Each app's `main.c` is its whole story: the client endpoint for the peer, statically registered hooks that post to the app's own queue, and sync calls by generated item ID. The client bindings are generated from the other app's `stow.yaml`.

`common` holds what both share: the link, a test console, `prj.conf` and board files. `tests` runs both as a simulated network with the [link broker](../../docs/source/link_broker/index.rst), following `topology.yaml`.

## Simulated network

On Linux, or on Windows in WSL, inside `nix develop`:

```sh
just test-peers
```

## Two boards

Build each app for the nRF52840 DK:

```sh
west build -b nrf52840dk/nrf52840 -d build/controller samples/stow_peers/controller
west build -b nrf52840dk/nrf52840 -d build/sensor samples/stow_peers/sensor
```

The link is `uart1`: wire P1.02 (TX) of each board to P1.01 (RX) of the other, and join their grounds. The console is on the J-Link USB serial port. On each console, enter `link up`, then `peer connect` once both are up.

## Test console

| Command | Prints |
| --- | --- |
| `link up`, `link down` | `@link up <err>`, `@link down` |
| `peer connect` | `@connect <err>` |
| `peer get <item>`, `peer set <item> <value>` | `@sent <n>`, then `@done <n> <err> [<item>=<value>]` |
| `local get <item>`, `local set <item> <value>` | `@local <err> [<item>=<value>]` |

Client hooks print `@connected <err>`, `@disconnected` and `@update <item>=<value>`.
