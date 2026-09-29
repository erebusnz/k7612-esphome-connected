"""Send one output-test command at 9600 8N1 and print its acknowledgment."""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import time
import serial

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('port')
parser.add_argument('command', help='Quoted command, e.g. "LED1 Y", "POL LED1 H", "RELEASE", or "?"')
args = parser.parse_args()
if '\r' in args.command or '\n' in args.command or len(args.command) > 23:
    parser.error('Use one command of at most 23 ASCII characters')
try:
    payload = (args.command + '\n').encode('ascii')
except UnicodeEncodeError:
    parser.error('Commands must be ASCII')
folder = Path(__file__).resolve().parents[1] / 'captures'
folder.mkdir(exist_ok=True)
path = folder / 'output-commands.jsonl'
success = False
with serial.Serial(args.port, 9600, timeout=.2, write_timeout=2) as uart, path.open('a', encoding='utf-8') as log:
    time.sleep(.3)
    uart.reset_input_buffer()
    log.write(json.dumps({'utc': datetime.now(timezone.utc).isoformat(), 'sent': args.command})+'\n')
    uart.write(payload)
    deadline = time.monotonic() + 4
    pending = bytearray()
    while time.monotonic() < deadline:
        pending.extend(uart.read(max(1, min(uart.in_waiting, 4096))))
        while b'\n' in pending:
            line, _, pending = pending.partition(b'\n')
            text = line.decode('ascii', errors='backslashreplace').strip()
            print(text, flush=True)
            log.write(json.dumps({'utc': datetime.now(timezone.utc).isoformat(), 'received': text})+'\n')
            if text.startswith('ERR '):
                raise SystemExit(1)
            if text.startswith('OK ') or (args.command.strip() == '?' and text.startswith('STATE RAW=')):
                success = True
        if success:
            break
if not success:
    raise SystemExit('No acknowledgment received; output state is unconfirmed')
