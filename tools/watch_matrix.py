"""Record matrix-test UART output with timestamps and a key-event summary."""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import time
import serial


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('port')
    parser.add_argument('--seconds', type=float, default=600)
    args = parser.parse_args()
    capture_dir = Path(__file__).resolve().parents[1] / 'captures'
    capture_dir.mkdir(exist_ok=True)
    stamp = datetime.now(timezone.utc).strftime('%Y%m%d-%H%M%SZ')
    path = capture_dir / f'matrix-test-{stamp}.jsonl'
    print(f'Recording to {path}', flush=True)
    seen_identity = False
    seen_state = False
    downs = []
    start = time.monotonic()
    pending = bytearray()
    with path.open('w', encoding='utf-8') as log, serial.Serial(
        args.port, 9600, timeout=0.2, write_timeout=2
    ) as uart:
        time.sleep(0.3)
        uart.reset_input_buffer()
        uart.write(b'?')
        while time.monotonic() - start < args.seconds:
            pending.extend(uart.read(max(1, min(uart.in_waiting, 4096))))
            while b'\n' in pending:
                line, _, pending = pending.partition(b'\n')
                text = line.decode('ascii', errors='backslashreplace').strip()
                elapsed = round(time.monotonic() - start, 3)
                log.write(json.dumps({'utc': datetime.now(timezone.utc).isoformat(),
                                      'elapsed_s': elapsed, 'raw': text}) + '\n')
                log.flush()
                if text in ('K7612 MATRIX TEST v1 9600', 'K7612 MATRIX TEST v2 9600',
                            'K7612 MATRIX TEST v3 9600'):
                    seen_identity = True
                if text.startswith('STATE RAW='):
                    if not seen_state:
                        print(f'{elapsed:8.3f}s {text}', flush=True)
                    seen_state = True
                else:
                    print(f'{elapsed:8.3f}s {text}', flush=True)
                if text.startswith('KEY DOWN '):
                    downs.append(text.split()[2])
            if len(pending) > 4096:
                log.write(json.dumps({'error': 'Unframed serial bytes', 'hex': pending.hex()})+'\n')
                raise RuntimeError('No line endings in serial data; raw bytes saved')
    print(f'Identity={seen_identity}, status={seen_state}, pressed={" ".join(downs) or "none"}', flush=True)
    return 0 if seen_identity and seen_state else 1


if __name__ == '__main__':
    raise SystemExit(main())
