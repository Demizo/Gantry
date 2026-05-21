# Example App

A sample application using the Gantry library. It includes:

- Using the Stow to store device information, configuration, and state (defined by the `stow.yaml` file)
- Using the Stow Protocol over UART and BLE
- Configuring reference counted memory pools

## Building and Flashing

Build for the nRF52840 DK:

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

Stream RTT logs from the running device:

```sh
just rtt
```

## Testing

The integration tests exercise the Stow Protocol over a live UART connection. A device running the example app firmware must be connected and flashed before running them.

Tests are located in `tests/integration/` and use `pytest` via `uv`.

### Running

From the example app folder, pass the serial port of the connected device:

```sh
uv run pytest -s tests --uart-port /dev/ttyACM0
```

The port can also be set via the `STOW_UART_PORT` environment variable:

```sh
export STOW_UART_PORT=/dev/ttyACM0
uv run pytest -s tests
```
