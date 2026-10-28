# K7612 keypad and RFID firmware

Replacement firmware for the **STC8C2K32S2** in the K7612 access keypad.
It reads the 12-key matrix and EM4100 RFID tags concurrently, reports events
over UART, and generates a confirmation tone for each key press or new tag.
OPEN selects red/green status; submitting a PIN with `#` flashes both LEDs.

<img src="docs/images/k7612-keypad.png" alt="K7612 access keypad with a metal housing and 12-key numeric keypad" width="280">

The primary build is `keypad_reader`. It starts automatically at power-on and
uses fixed-memory, interrupt-assisted acquisition suitable for the 8-bit MCU.
Wiegand-26/34 output supports RFID and decimal PINs submitted with `#`;
The main build enables output at boot using the sense-pin-verified active-HIGH
driver polarity. ESPHome integration, PIN authorization,
credential storage and relay control are not implemented in `keypad_reader`. This is reader firmware, not a complete
access-control system.

## Features

- Debounced `1`â€“`9`, `*`, `0`, `#` press and release events; no held-key repeat.
- EM4100 RF/64 Manchester decoding with header, row parity, column parity and
  stop-bit validation. Invalid frames are rejected without bit repair.
- One RFID notification per presentation; removal rearms notification after
  1.5 seconds without a matching frame or eligible presence prefix.
- Shared non-blocking buzzer queue: approximately 204 ms at 2.5 kHz per event,
  with at least 20 ms between tones.
- Wiegand-26/34: RFID low 24/32 bits; buffered decimal PIN submission with `#`.
  See [configuration and polarity probe](docs/keypad-reader.md#wiegand-output-and-pins).
- Active-HIGH red/green LEDs follow OPEN, with a 150 ms both-on flash for
  a nonempty, valid PIN submitted using `#`.
- UART at **9600 baud, 8N1**, with acquisition and status diagnostics.

See the [reader interface](docs/keypad-reader.md) for events and commands and
the [firmware architecture](docs/firmware-architecture.md) for implementation
details, resource use and limitations.

## Hardware

The supported target is the **STC8C2K32S2, LQFP32, at 24 MHz**. Check the MCU
and board routing before using this firmware on another K7612 revision.

| Function | MCU signals | Physical pins |
| --- | --- | --- |
| Crystal XTALO / XTALI | P1.6 / P1.7 | 7 / 8 |
| UART RX / TX | P3.0 / P3.1 | 13 / 14 |
| Matrix rows | P0.0, P0.1, P0.2, P0.3 | 29, 30, 31, 32 |
| Matrix columns | P1.5, P1.4, P1.3 | 6, 5, 4 |
| RFID carrier drive | P3.5 / T0CLKO | 18 |
| RFID comparator inputs | P3.6, P3.7 | 19, 20 |
| Buzzer drive | P3.4 | 17 |
| Relay drive (active high, bench-confirmed) | P2.3 | 24 |
| LED1 green (bench-confirmed) | P2.6 | 27 |
| LED2 red (bench-confirmed) | P2.7 | 28 |
| OPEN input (active low) | P2.2 | 23 |
| D0 green wire output | P5.4 | 9 |
| D1 white wire output | P5.5 | 11 |
| D0 green wire (input sense) | P3.3 | 16 |
| D1 white wire (input sense) | P3.2 | 15 |

Rows use open-drain outputs and columns use inputs with pull-ups. The scanner
releases each row between selections. The carrier is nominally 125 kHz.

The main connector's OPEN wire, beside D0 and D1, connects to P2.2 / pin 23.
OPEN HIGH selects red; OPEN LOW selects green. Relay activation is not
implemented in the main firmware.

### Programming connection

Use a compatible logic-level USB-to-UART adapter. The supported Arduino Micro
bridge is [ProMicroUartBridge.ino](tools/ProMicroUartBridge/ProMicroUartBridge.ino);
upload it using the Arduino's actual board profile before programming the STC.

| Adapter | Keypad |
| --- | --- |
| TX | MCU RX |
| RX | MCU TX |
| GND | GND |
| Power output | Disconnected; keypad uses its own supply |

The target UART operates in a 5 V signal domain. A 3.3 V adapter or ESP32 needs
appropriate level translation. Identify connector signals by function;
numbered connector positions are not established here.

The MCU must fully power down before entering its bootloader. Prevent adapter
TX from backfeeding the unpowered target. For a 5 V Arduino Micro, the established
arrangement uses a Schottky diode with cathode toward Arduino TX and anode
toward keypad RX, plus a pull-up from keypad RX to the switched MCU 5 V supply.
Verify that MCU VCC falls close to zero when target power is removed.

**Programming replaces the installed firmware.** Retaining factory operation
requires a separate backup or an unmodified unit. The
[factory UART interface](docs/using-unflashed-k7612.md) is an alternative to
installing this firmware and uses a different protocol.

## Build

The development toolchain is Python 3.12, PlatformIO and SDCC. Dependencies and
the PlatformIO platform version are pinned in the repository.

```powershell
python -m venv .venv
.venv/Scripts/python.exe -m pip install -r requirements-tools.txt
.venv/Scripts/pio.exe run
```

These examples use PowerShell on Windows. On other hosts, use the virtual
environment's corresponding executable paths; those hosts have not been
hardware-qualified. Building does not program the MCU.

| Environment | Purpose |
| --- | --- |
| `keypad_reader` (default) | Combined keypad, RFID and confirmation tones |
| `rfid_firmware_test` | RFID acquisition diagnostics without keypad scanning |
| `matrix_test` | Standalone matrix and direct LED/buzzer output diagnostics |
| `uart_smoke`, `pin_watch` | Low-level UART and GPIO diagnostics |

Select another environment with `pio run -e <environment>`.

## Flash

```powershell
.venv/Scripts/python.exe -m serial.tools.list_ports -v
powershell -NoProfile -ExecutionPolicy Bypass -File tools/Flash-STC.ps1 -Port COM14 -Environment keypad_reader
```

Replace `COM14` with the adapter's port. Close other serial applications, keep
the USB adapter powered, and cycle target power when prompted. The script
builds first and stops on compilation failure. It uses stcgal's `stc8d`
protocol, 2400-baud bootloader detection, 19200-baud programming and 24 MHz
oscillator calibration for the supported MCU/bootloader configuration.

## Use

The combined reader starts with UART debugging disabled and operates through
Wiegand and OPEN without a UART connection. To diagnose it, open a terminal at
9600 baud, 8N1, send `!`, wait 500 ms, then send `DEBUG ON` and `READ`, each
followed by Enter. Diagnostic output includes:

```text
KEY DOWN 5
KEY UP 5
Frame hex: FF8060280C048142
Data: 0102030405
BEEP DONE
TAG ABSENT REARMED
```

Each key press and newly acquired tag requests a tone. Release events do not.
Multiple simultaneous keys block new presses until all keys are released.
Periodic `READ` lines report acquisition and queue diagnostics.

The reader starts automatically. Send `!` to stop it before issuing a command;
then enable `DEBUG ON` if needed and send `?` to query status or `READ` followed
by a newline to restart. To resume silently, send `DEBUG OFF` before `READ`.
Every reset restores debugging OFF.
Commands for the separate `matrix_test` environment, such as direct LED
control, are not supported by `keypad_reader`.

## Validate changes

After installing the toolchain with a build:

```powershell
.venv/Scripts/python.exe tools/test_reader.py
```

The suite compiles actual C code and runs it in the 8051 simulator. It covers
frame validation, sampling and timer wrap, debounce, multi-key handling, tone
queuing, presence suppression and interrupt storage isolation. Fixtures are
generated by the tests; no local captures or connected hardware are needed.

Tests locate SDCC and `s51` in PlatformIO's toolchain, or via `SDCC_BIN` or
`PATH`. The generic simulator does not model the STC analog front end or prove
real-time hardware margins. Physical validation has covered tag presentation
and removal, keypad presses, held-key suppression and tone feedback on the
development board. Reception range, other board revisions and long-term
reliability remain unqualified.

Keep changes bounded and non-blocking, run the simulator suite, and build both
reader environments before submitting a change. Document changes to UART
events, pin assignments or timer ownership. Generated builds, local captures
and machine-specific equipment settings do not belong in firmware changes.
