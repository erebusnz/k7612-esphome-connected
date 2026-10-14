"""Run the firmware regression suite without serial devices or saved captures."""
from pathlib import Path
import subprocess
import sys


def main():
    tools = Path(__file__).resolve().parent
    suites = [
        ('test_keypad_reader.py',),
        ('test_rfid_decoder.py', '--header'),
        ('test_rfid_decoder.py', '--clock-tracking'),
        ('test_rfid_decoder.py',),
        ('test_rfid_streaming.py', '--header', '--combined'),
        ('test_rfid_streaming.py', '--header'),
        ('test_rfid_streaming.py', '--clock-tracking'),
        ('test_rfid_firmware.py',),
        ('test_rfid_interrupt.py', '--combined'),
        ('test_rfid_interrupt.py',),
    ]
    for script, *arguments in suites:
        print(f'\nRunning {script} {" ".join(arguments)}', flush=True)
        result = subprocess.run([sys.executable, str(tools / script), *arguments],
                                cwd=tools.parent, timeout=180)
        if result.returncode:
            return result.returncode
    print('\nAll firmware regression suites passed.')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
