"""Verify the flashed UART test image through the Micro bridge, saving evidence."""
import argparse
from datetime import datetime, timezone
from pathlib import Path
import time

import serial


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port")
    args = parser.parse_args()
    received = bytearray()
    identity = b"K7612 UART TEST v1 STC8C2K32S2 24000000 9600 OK\r\n"
    heartbeat = b"K7612 UART TEST v1 ALIVE\r\n"
    with serial.Serial(args.port, 9600, timeout=0.2, write_timeout=2) as uart:
        time.sleep(0.3)  # Allow the USB bridge to apply new UART line coding.
        uart.reset_input_buffer()
        deadline = time.monotonic() + 10
        next_query = 0
        while time.monotonic() < deadline:
            now = time.monotonic()
            if now >= next_query:
                uart.write(b"?")
                next_query = now + 2
            received.extend(uart.read(256))
            if identity in received and received.count(heartbeat) >= 3:
                break
    output = Path(__file__).resolve().parents[1] / "captures"
    output.mkdir(exist_ok=True)
    stamp = datetime.now(timezone.utc).strftime("%Y%m%d-%H%M%SZ")
    path = output / f"uart-smoke-{stamp}.bin"
    path.write_bytes(received)
    print(received.decode("ascii", errors="backslashreplace"))
    print(f"Raw capture: {path}")
    passed = identity in received and received.count(heartbeat) >= 3
    print("PASS: identity reply and three heartbeats" if passed else "FAIL: required replies missing")
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
