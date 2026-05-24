import argparse
import subprocess
import socket
import time
import sys
import threading
import queue


def monitor_openocd(process, event_queue):
    """Reads OpenOCD output and signals if a reset/crash occurs."""
    for line in iter(process.stdout.readline, b""):
        decoded = line.decode("utf-8", errors="replace")

        if (
            "Failed to read up-channel" in decoded
            or "external reset detected" in decoded
        ):
            event_queue.put("RESTART")


def run_rtt_session(args, ram_base, ram_size):
    """Runs a single OpenOCD session. Returns True if user exits, False if it needs a restart."""
    openocd_cmd = [
        "openocd",
        "-f",
        args.config,
        "-c",
        "init",
        "-c",
        f"rtt server start {args.port} 0",
        "-c",
        f'rtt setup {ram_base} {ram_size} "SEGGER RTT"',
        "-c",
        "rtt start",
    ]

    # Use PIPE so we can intercept the logs
    process = subprocess.Popen(
        openocd_cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT
    )

    # Start the monitoring thread
    event_queue = queue.Queue()
    monitor_thread = threading.Thread(
        target=monitor_openocd, args=(process, event_queue), daemon=True
    )
    monitor_thread.start()

    # Wait for OpenOCD to open the port
    s = None
    for attempt in range(10):
        try:
            s = socket.create_connection(("localhost", args.port), timeout=1)
            s.settimeout(0.5)
            break
        except (ConnectionRefusedError, socket.timeout):
            time.sleep(0.5)

    if not s:
        print("\n[!] Error: Could not connect to RTT server. Retrying...")
        process.terminate()
        process.wait()
        return False

    try:
        print("--- Connected to RTT stream (Press Ctrl+C to exit) ---")
        while True:
            try:
                event = event_queue.get_nowait()
                if event == "RESTART":
                    print("\n[!] Board reset detected. Restarting RTT connection...")
                    return False
            except queue.Empty:
                pass

            if process.poll() is not None:
                print("\n[!] OpenOCD process unexpectedly terminated. Retrying...")
                return False

            # Read RTT logs
            try:
                data = s.recv(1024)
                if not data:
                    break
                sys.stdout.write(data.decode("utf-8", errors="replace"))
                sys.stdout.flush()
            except (socket.timeout, TimeoutError):
                continue

    except KeyboardInterrupt:
        print("\n--- Disconnecting ---")
        return True
    finally:
        if s:
            s.close()

        process.terminate()
        try:
            process.wait(timeout=2)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()


def main():
    parser = argparse.ArgumentParser(description="View RTT logs via OpenOCD")
    parser.add_argument(
        "--device", required=True, help="Device type (e.g., nrf52840dk/nrf52840)"
    )
    parser.add_argument("--config", required=True, help="Path to openocd.cfg")
    parser.add_argument(
        "--port", type=int, default=8765, help="RTT Telnet port (default: 8765)"
    )
    args = parser.parse_args()

    device_name = args.device.split("/")[-1] if "/" in args.device else args.device
    ram_base = "0x20000000"
    ram_size = "0x40000"

    print(f"Starting OpenOCD RTT server for {device_name} on port {args.port}...")

    while True:
        exit_cleanly = run_rtt_session(args, ram_base, ram_size)
        if exit_cleanly:
            break


if __name__ == "__main__":
    main()
