"""Binary loopback check for ProMicroUartBridge (disconnect the target first)."""

import argparse
import sys
import time

import serial


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port", help="Pro Micro port, for example COM14")
    args = parser.parse_args()
    settings = [(2400, "E"), (19200, "E"), (115200, "E"),
                (115200, "N"), (2400, "E")]
    payload = bytes(range(256))
    failures = 0
    try:
        with serial.Serial(args.port, baudrate=2400, parity="E", timeout=2,
                           write_timeout=2) as port:
            for baud, parity in settings:
                port.baudrate = baud
                port.parity = parity
                # Give the bridge time to apply both USB line-coding updates.
                time.sleep(0.2)
                port.reset_input_buffer()
                received = bytearray()
                for offset in range(0, len(payload), 32):
                    chunk = payload[offset:offset + 32]
                    port.write(chunk)
                    port.flush()
                    echo = port.read(len(chunk))
                    received.extend(echo)
                    if echo != chunk:
                        break
                time.sleep(0.1)
                extra = port.read(port.in_waiting)
                passed = received == payload and not extra
                print(f'{baud} 8{parity}1: {"PASS" if passed else "FAIL"} '
                      f'({len(received)}/{len(payload)} bytes, '
                      f'{len(extra)} extra)', flush=True)
                if not passed:
                    failures += 1
                    print(f'  Received prefix: {received[:32].hex(" ")}', flush=True)
    except (serial.SerialException, OSError) as exc:
        print(f"Serial error: {exc}", file=sys.stderr)
        return 2
    print("Serial port closed. Remove the loopback wire before reconnecting the keypad.")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
