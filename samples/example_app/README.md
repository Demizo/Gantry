# Example App

A sample application using the Gantry library. It includes:

- Using the Stow to store device information, configuration, and state (defined by the `stow.yaml` file)
- Using the Stow Protocol over UART and BLE
- Configuring reference counted memory pools

## Supported Boards

| Board | Board target | probe-rs chip | Stow serial port |
| --- | --- | --- | --- |
| nRF52840 DK | `nrf52840dk/nrf52840` (default) | `nRF52840_xxAA` | The DK's only serial port, often `/dev/ttyACM0` |
| nRF54L15 DK | `nrf54l15dk/nrf54l15/cpuapp` | `nRF54L15` | VCOM1, the port that prints the boot banner |
| nRF54LM20 DK | `nrf54lm20dk/nrf54lm20a/cpuapp` | `nRF54LM20A` | VCOM1, the port that prints the boot banner |

The Stow serial port is the console UART, which reaches the host through the DK's onboard debugger.

## Building and Flashing

Build for the nRF52840 DK:

```sh
just build
```

Pick another board by overriding `board`:

```sh
just board=nrf54l15dk/nrf54l15/cpuapp build
just board=nrf54l15dk/nrf54l15/cpuapp flash
```

For a pristine rebuild:

```sh
just rebuild
```

Flash to a connected board with probe-rs, and reset it:

```sh
just flash
```

Stream RTT logs from the running device:

```sh
just rtt
```

`just flash` and `just rtt` share the probe, so stop `just rtt` before flashing and run it again afterwards. If the logs stop after a reset, run it again too.

## Testing

The integration tests exercise the Stow Protocol over a live UART connection. A device running the example app firmware must be connected and flashed before running them.

Tests are located in `tests/` and use `pytest` via `uv`.

### Running

Flash the board, then pass its Stow serial port from the table above:

```sh
just example_app test -s --uart-port /dev/ttyACM0
```

The port can also be set via the `STOW_UART_PORT` environment variable:

```sh
export STOW_UART_PORT=/dev/ttyACM0
just example_app test -s
```
