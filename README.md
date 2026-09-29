# K7612 replacement firmware

Replacement firmware for the K7612 keypad's **STC8C2K32S2** MCU, with the goal of exposing key events and LED/buzzer control over UART to an ESP32 running ESPHome.

This repository contains the MCU firmware, Arduino UART bridge, flashing tools, and measured pin mappings. The current firmware is a working bench diagnostic; ESPHome integration and replacement lock-control logic are not implemented yet.

## Firmware status

The installed **matrix/output test v3** builds to 3,975 bytes and communicates at **9600 baud, 8N1**.

| Feature | Status |
| --- | --- |
| 12-key matrix | All keys and a held-key test passed on v2; scan retained in v3, full v3 key regression pending |
| LED1 / P2.6 / chip pin 27 | On/off verified, active-low |
| LED2 / P2.7 / chip pin 28 | On/off verified, active-low |
| Buzzer / P3.4 / chip pin 17 | Both GPIO transitions produce a click; tone generation is next |
| Serial output commands | Implemented and tested on hardware |
| ESPHome and lock control | Not implemented |

The buzzer behavior is consistent with a passive transducer. The current command applies a steady level; it does not generate a tone. Outputs start in high-impedance mode. Next steps are timer-driven buzzer tones, a full key regression on v3, and ESPHome integration.

## Build and program the MCU

The tested setup uses an **Arduino Micro as a USB-to-UART bridge**, with PlatformIO/SDCC for compilation and stcgal for STC programming. Upload [ProMicroUartBridge.ino](tools/ProMicroUartBridge/ProMicroUartBridge.ino) to the Arduino Micro first, selecting its actual board profile. Programming the STC afterward does not replace the Arduino's bridge firmware.

### Connect the programmer

For the tested 5 V Arduino Micro and 5 V keypad MCU:

| Arduino Micro | Keypad |
| --- | --- |
| D1 / TX | MCU RX |
| D0 / RX | MCU TX |
| GND | GND |
| VCC / RAW | Leave disconnected; use the keypad's normal supply |

Identify the UART header signals by function: its numbered connector pinout remains unverified. Swapped RX/TX wiring caused earlier programming failures; correcting it restored flashing.

The keypad must fully power down to enter its bootloader on the next power-on. Arduino TX can backfeed the unpowered MCU. A Schottky diode with its **band/cathode toward Arduino TX** and anode toward keypad RX blocks that path; a pull-up from keypad RX to the keypad's switched MCU +5 V supplies the high level. Verify power-off MCU VCC falls close to zero. A series resistor alone left our MCU at 1.5 V and did not solve the backfeeding.

**Flashing replaces the original firmware.** The development unit's original firmware was overwritten without a backup. If you want to retain factory behavior, see the original-firmware option below before flashing.

### Set up and flash

Run from the repository root in PowerShell. Python 3.12 was used for the tested setup.

```powershell
python -m venv .venv
.\.venv\Scripts\python.exe -m pip install -r requirements-tools.txt
.\.venv\Scripts\python.exe -m serial.tools.list_ports -v
.\.venv\Scripts\pio.exe run -e matrix_test
.\tools\Flash-STC.ps1 -Port COM14 -Environment matrix_test
```

Replace `COM14` with the Arduino's current port. Close other serial monitors, keep Arduino USB powered, and power-cycle the keypad when stcgal shows **Waiting for MCU**. The flash script rebuilds before programming and stops if compilation fails.

The verified programmer settings are **`-P stc8d -l 2400 -b 19200 -t 24000`**. The script supplies them automatically; automatic `stc8` selection previously failed during clock calibration. These settings worked with the tested MCU's 7.3.13U bootloader.

### Verify and send commands

After flashing, query the firmware or record key events:

```powershell
.\.venv\Scripts\python.exe tools/send_output.py COM14 "?"
.\.venv\Scripts\python.exe tools/watch_matrix.py COM14 --seconds 60
```

Run one serial tool at a time. After the key logger exits, test outputs with:

```powershell
.\.venv\Scripts\python.exe tools/send_output.py COM14 "LED1 Y"
.\.venv\Scripts\python.exe tools/send_output.py COM14 "LED1 N"
```

The same `Y`/`N` commands exist for `LED2` and `BUZZER`. Commands are case-insensitive ASCII lines terminated by CR, LF, or CRLF; wait for an acknowledgment before sending the next command. Default active-low polarity means Y drives low and N drives high. Both LED polarities are verified; buzzer Y/N currently only tests steady levels.

| Command | Behavior |
| --- | --- |
| `LED1 Y/N`, `LED2 Y/N`, `BUZZER Y/N` | Drive the selected output; replies include the actual requested high/low level |
| `POL LED1 H/L` | Select active-high/low polarity and release that output; also accepts LED2 or BUZZER |
| `RELEASE` | Return all three outputs to high impedance |
| `?` | Report firmware identity, configuration, outputs, and matrix state |
| `DIAG Y/N` | Enable/disable per-row diagnostics; disabled by default |

Settings are not persisted. High impedance does not guarantee an external circuit is off. Lines are limited to 23 characters; malformed commands return `ERR`. After a receive-buffer overflow, send a newline to resynchronize. Key events appear as `KEY DOWN 1 R0 C0` and `KEY UP 1 R0 C0`; multiple detected keys are blocked until all are released.

## Matrix and development tools

The measured and tested matrix uses these physical LQFP32 chip pins:

| Row | Pin 6 / P1.5 | Pin 5 / P1.4 | Pin 4 / P1.3 |
| --- | --- | --- | --- |
| Pin 29 / P0.0 | 1 | 2 | 3 |
| Pin 30 / P0.1 | 4 | 5 | 6 |
| Pin 31 / P0.2 | 7 | 8 | 9 |
| Pin 32 / P0.3 | * | 0 | # |

[Main firmware](src/matrix_test.c) selects one open-drain row low at a time and reads high-impedance columns. All seven matrix pins have explicit internal pull-ups and digital inputs enabled. Each row has a 1 ms release and 1 ms selection settling interval. This resolved the earlier false whole-column detections. GPIO setup follows chapter 9 of the [STC8C manual](https://www.stcmicro.com/datasheet/STC8C-cn.pdf).

The optional chip-pin mapping UI, server, and raw measurements are kept together in the gitignored `local-continuity/` folder on the development computer. They are not included in a fresh clone; the verified matrix above is the firmware reference. If the local folder is present, launch it with:

```powershell
.\.venv\Scripts\python.exe local-continuity/serve_continuity.py
```

Open http://127.0.0.1:8765/. Changes save to `local-continuity/continuity-map.json`; output candidates, undo, and JSON export are also supported.

`matrix_test` is the default build target; `pin_watch` and `uart_smoke` retain earlier diagnostics. Run `tools/test_output_commands.py` with the project Python to test the real command parser in the SDCC 8051 simulator (35 cases). Raw serial logs and flash evidence remain in `captures/`, including `matrix-test-20260929-101431Z.jsonl`, `matrix-flash-v3.log`, and `output-commands.jsonl`.

## Using the original firmware instead

An unmodified keypad can also communicate through its **TX/RX pin header** using the factory **Tuya MCU UART protocol at 9600 baud, 8N1**. For an ESP32 or another 3.3 V controller, use a suitable **5 V ↔ 3.3 V UART level shifter**, crossing TX to RX in each direction and sharing ground. Keep the keypad on its own supply.

Factory-firmware lock/unlock through **DP1** was physically verified; individual key events and card IDs were not established. This is an alternative to flashing, and its binary protocol differs from the replacement firmware's text commands. See [using the original firmware](docs/using-unflashed-k7612.md) for wiring and protocol details.
