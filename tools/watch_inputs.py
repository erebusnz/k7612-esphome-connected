"""Log pin-watch frames and decode STC8C2K32S2 LQFP32 pin transitions."""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import time

import serial

PORTS = (0, 1, 2, 3, 5)
MASKS = (0x0F, 0xFF, 0xFF, 0xFC, 0x30)
# Manufacturer's LQFP32 diagram; these are chip pins, not header pins.
PINS = {(0, b): 29 + b for b in range(4)}
PINS.update({(1, b): 1 + b for b in range(8)})
PINS.update({(2, b): 21 + b for b in range(8)})
PINS.update({(3, b): 13 + b for b in range(2, 8)})
PINS.update({(5, 4): 9, (5, 5): 11})


def decode(line):
    fields = line.split()
    if not fields or fields[0] not in {"EDGE", "BASE", "STATE", "LIVE"}:
        return None
    edge = fields[0] == "EDGE"
    if len(fields) != (11 if edge else 6):
        raise ValueError("incorrect frame length")
    values = [int(value, 16) for value in fields[1:]]
    if any(value & ~MASKS[i % 5] for i, value in enumerate(values)):
        raise ValueError("frame includes unmonitored pins")
    changes = []
    if edge:
        for port, state, mask in zip(PORTS, values[:5], values[5:]):
            for bit in range(8):
                if mask & (1 << bit):
                    changes.append({"gpio": f"P{port}.{bit}", "chip_pin": PINS[port, bit],
                                    "old": 1 - ((state >> bit) & 1), "new": (state >> bit) & 1})
    return {"kind": fields[0], "states": dict(zip((f"P{p}" for p in PORTS), values[:5])),
            "changes": changes}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port")
    parser.add_argument("--seconds", type=float, default=600)
    parser.add_argument("--label", default="unlabelled")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1] / "captures"
    root.mkdir(exist_ok=True)
    stamp = datetime.now(timezone.utc).strftime("%Y%m%d-%H%M%SZ")
    path = root / f"pin-watch-{stamp}.jsonl"
    print(f"Recording {args.seconds:g}s to {path}", flush=True)
    identity_seen = False
    state_seen = False
    edges = 0
    start = time.monotonic()
    with path.open("w", encoding="utf-8") as log, serial.Serial(
        args.port, 9600, timeout=0.25, write_timeout=2
    ) as uart:
        time.sleep(0.3)
        uart.reset_input_buffer()
        uart.write(b"?")
        pending = bytearray()
        while time.monotonic() - start < args.seconds:
            pending.extend(uart.read(max(1, min(uart.in_waiting, 4096))))
            while b"\n" in pending:
                line, _, pending = pending.partition(b"\n")
                text = line.decode("ascii", errors="backslashreplace").strip()
                event = {"utc": datetime.now(timezone.utc).isoformat(),
                         "elapsed_s": round(time.monotonic() - start, 3),
                         "label": args.label, "raw": text}
                try:
                    event["decoded"] = decode(text)
                except ValueError as error:
                    event["error"] = str(error)
                log.write(json.dumps(event) + "\n")
                log.flush()
                if text.startswith("K7612 PIN WATCH v1 HIZ"):
                    identity_seen = True
                    print(text, flush=True)
                decoded = event.get("decoded")
                if decoded:
                    state_seen = True
                    if decoded["kind"] != "LIVE":
                        print(f"{event['elapsed_s']:8.3f}s {text}", flush=True)
                    for change in decoded["changes"]:
                        edges += 1
                        print(f"  {change['gpio']} (chip pin {change['chip_pin']}): "
                              f"{change['old']} -> {change['new']}", flush=True)
            if len(pending) > 4096:
                raise RuntimeError("Serial stream has no frame terminators")
    print(f"Finished: identity={identity_seen}, state={state_seen}, pin transitions={edges}", flush=True)
    return 0 if identity_seen and state_seen else 1


if __name__ == "__main__":
    raise SystemExit(main())
