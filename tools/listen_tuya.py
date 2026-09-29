"""Passively log Tuya bytes from an already configured Bus Pirate UART console."""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import re
import time
import serial


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', default='COM8')
    parser.add_argument('--seconds', type=float, default=120)
    args = parser.parse_args()
    folder = Path(__file__).resolve().parents[1] / 'captures'
    stem = folder / datetime.now().strftime('tuya-experiment-%Y%m%d-%H%M%S')
    folder.mkdir(exist_ok=True)
    text = ''
    consumed = 0
    buffer = bytearray()
    total = 0
    start = time.monotonic()
    with serial.Serial(args.port, 115200, timeout=0.2, write_timeout=2) as port, \
            stem.with_suffix('.jsonl').open('w', encoding='utf-8') as events:
        time.sleep(0.4)
        port.write(b'{\r')  # Open asynchronous receive; sends no target bytes.
        print(f'Listening for {args.seconds:g} seconds. Log: {stem}', flush=True)
        try:
            while time.monotonic() - start < args.seconds:
                chunk = port.read(port.in_waiting or 1)
                if not chunk:
                    continue
                text += chunk.decode(errors='replace')
                tokens = re.findall(r'0x([0-9a-fA-F]{2})', text)
                buffer.extend(int(value, 16) for value in tokens[consumed:])
                consumed = len(tokens)
                while len(buffer) >= 7:
                    header = buffer.find(b'\x55\xaa')
                    if header < 0:
                        buffer[:] = buffer[-1:]
                        break
                    del buffer[:header]
                    if len(buffer) < 7:
                        break
                    length = int.from_bytes(buffer[4:6], 'big')
                    if length > 4096:
                        del buffer[0]
                        continue
                    if len(buffer) < length + 7:
                        break
                    frame = bytes(buffer[:length + 7])
                    if sum(frame[:-1]) & 255 != frame[-1]:
                        del buffer[0]
                        continue
                    del buffer[:length + 7]
                    event = {'utc': datetime.now(timezone.utc).isoformat(),
                             'elapsed_s': round(time.monotonic() - start, 3),
                             'frame_hex': frame.hex(' '), 'command': frame[3],
                             'checksum_valid': True, 'dps': []}
                    if frame[3] == 7:
                        payload = frame[6:-1]
                        while len(payload) >= 4:
                            dp, kind = payload[:2]
                            size = int.from_bytes(payload[2:4], 'big')
                            if len(payload) < size + 4:
                                break
                            value = payload[4:4 + size]
                            decoded = (bool(value[0]) if kind == 1 and size == 1
                                       else int.from_bytes(value, 'big') if kind in (2, 4, 5)
                                       else value.hex(' '))
                            event['dps'].append({'id': dp, 'type': kind, 'value': decoded})
                            payload = payload[4 + size:]
                    events.write(json.dumps(event) + '\n')
                    events.flush()
                    total += 1
                    print(json.dumps(event), flush=True)
        finally:
            port.write(b'}\r')
            time.sleep(0.1)
            stem.with_suffix('.txt').write_text(text, encoding='utf-8')
    print(f'Finished: {total} valid frames. Receive closed; port released.', flush=True)


if __name__ == '__main__':
    main()
