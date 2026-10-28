# Reader interface

The default `keypad_reader` environment is the main K7612 firmware. It combines
keypad scanning, EM4100 reading, Wiegand output, button-press tones and OPEN-driven
LED indication. Services start automatically on power-on. UART is 9600 baud,
8 data bits, no parity and one stop bit, without flow control. UART debugging is
OFF after every power-on/reset: no banner, probe byte, events or periodic reports
are transmitted. Wiegand, OPEN LEDs, keypad and tones operate without a UART host.
The event descriptions below apply when debugging has been explicitly enabled.

The [hardware map](../README.md#hardware) identifies OPEN, LED and separate
D0/D1 input/output pins. D0/D1 are used by the Wiegand transmitter and polarity
probe. OPEN controls LED indication; relay control is not implemented.

## Keypad events

| Event | Meaning |
| --- | --- |
| `KEY DOWN <key>` | Debounced press; requests a tone |
| `KEY UP <key>` | Debounced release; no tone |
| `KEY CANCEL` | A held key became ambiguous because multiple keys were detected |

Keys are `123456789*0#`. A held key does not repeat. Five equal scans debounce
a transition; a full scan takes at least 8 ms. Multiple detected keys suppress
new presses until five scans confirm that all keys are released. Ambiguous
multi-key input also clears the pending PIN. PIN submission is described below;
this firmware does not authorize credentials or unlock a door.

| Row | P1.5 | P1.4 | P1.3 |
| --- | --- | --- | --- |
| P0.0 | 1 | 2 | 3 |
| P0.1 | 4 | 5 | 6 |
| P0.2 | 7 | 8 | 9 |
| P0.3 | * | 0 | # |

## RFID events

A newly acquired tag produces `Frame hex: <16 hex digits>` and
`Data: <10 hex digits>`. The frame includes EM4100 framing/parity; `Data`
contains the five-byte identifier. Notifications require a complete valid
frame, not a known-ID match. Tags transmit repeatedly, but the same presentation
does not produce repeated notifications.

Presence expires after 1.5 seconds without a valid matching frame or eligible
matching 32-bit prefix. A prefix only extends an already confirmed, unexpired
tag; it cannot acquire a tag, trigger a tone or revive expired presence. A
prefix is not a complete identity check and may be shared by different tags.
`TAG ABSENT REARMED` reports expiry. A different complete valid ID generates
a new notification immediately.

## Wiegand output and PINs

Both credential sources use the selected frame length (default **34**):

| Source | 26-bit payload | 34-bit payload |
| --- | --- | --- |
| RFID | Low 24 bits of the five-byte ID | Low 32 bits of the ID |
| PIN followed by `#` | Decimal integer, 0–16,777,215 | Decimal integer, 0–4,294,967,295 |
| `1234#` | `0004D2` | `000004D2` |

Payload bits are MSB first. Leading even parity covers the first 12/16 data
bits; trailing odd parity covers the last 12/16. These match the observed
Wiegand formats described above.
A conventional 26-bit facility/card decoder sees `1234#` as facility 0,
card 1234. Larger values use the upper payload byte as facility code.

Digits accumulate locally; only `#` submits a frame. Empty `#` does nothing.
`*`, ambiguous multi-key input, STOP, or ten seconds without a digit clear
entry. Out-of-range values or more than ten digits invalidate the entire
entry until clear/submit/timeout; an invalid entry never sends a truncated PIN.
Leading zeros are discarded: `001234#` equals `1234#`. PIN and RFID values
share a namespace; the receiver cannot distinguish the source from the frame.
Reserving a facility code, using packed BCD, or preserving PIN length would
require an agreed receiver-side convention and is not enabled here.

RFID output follows presence notifications, so a held tag does not repeatedly
transmit. UART capacity does not gate credential output. A fixed FIFO holds
two outstanding frames including any active frame; additional submissions
are dropped and counted by `WG_DROP`. Disabled output also increments this
counter on submission. There is no Wiegand acknowledgement or retry.

### Sense-pin polarity probe

Output MCU pins are P5.4/D0 and P5.5/D1; external-wire senses are P3.3/D0 and
P3.2/D1. Sense inputs are high impedance with no internal pull-ups. The main PlatformIO build sets `K7612_WIEGAND_ACTIVE=1`, verified using these
sense inputs on 2026-10-28, and enables Wiegand-34 automatically at boot.
The unconfigured source fallback keeps output high-Z until a **stopped-mode
`WGPROBE`** succeeds.
The probe is explicit, not an automatic test on every boot.

1. Send `!` and wait for `OK STOP`.
2. Send `WGPROBE` with CR/LF. It checks both wires idle HIGH, then drives each
   MCU output HIGH and LOW for 1 ms each, with 20 ms high-Z between tests.
3. Read `WG SENSE D0_HIGH/LOW D1_HIGH/LOW: aa bb cc dd` (hex masks of P3.3/P3.2).
   `04 0C 08 0C` means MCU HIGH pulls the wire LOW; `0C 04 0C 08` means MCU LOW.
   A failed idle, release, polarity or cross-coupling check leaves output disabled.
4. A pass reports `OK WG ACTIVE=0` or `1`. Send `WG26` or `WG34`, then `READ`.
5. Present a tag and enter `1234#`; verify the decoded values and pulse timing
   with the Arduino/scope. `?` while stopped reports length, enable and polarity.

The probe deliberately produces test pulses, so a connected decoder may report
an invalid/short frame. It establishes a digital polarity response only;
voltage levels, loading, rise times and timing still require bench checks.
Runtime settings are RAM-only. The main build already defines the verified
active-HIGH polarity. For another verified board, use
`-DK7612_WIEGAND_ACTIVE=0` or `=1` and `-DK7612_WIEGAND_BITS=34` (or `=26` for a 26-bit default)
to the `keypad_reader` build flags (retain `--std-sdcc11`) for automatic output
after reboot. The source fallback polarity is `-1` (disabled until probe); PlatformIO
overrides it to `1` for this board.

Timer1 schedules pulses alongside RF sampling. Nominal low width is 96 µs;
falling-edge spacing is 1,712 µs at the normal 16 µs sample cadence, or
1,728 µs in the 32 µs diagnostic mode. Frames have at least 150 ms idle between
them (150.000 ms at 16-microsecond sampling, 150.016 ms at 32-microsecond
sampling), measured from the last pulse release to the next frame's first
pulse. This leaves nominal margin beyond ESPHome's 100 ms receive timeout. These are scheduling targets, not measured hardware results. STOP
releases an active pulse immediately and discards pending frames/PINs;
the receiver may see a partial frame if stopped during transmission.

## OPEN and LED feedback

LED outputs are active HIGH: green on P2.6 and red on P2.7. OPEN is a
high-impedance input on P2.2 with internal pull-ups disabled.

| OPEN | Steady indication |
| --- | --- |
| HIGH (released) | Red on, green off |
| LOW (grounded) | Green on, red off |

The initial state is displayed at boot. During normal operation, a changed
OPEN level must remain stable for 20 ms. On submission of a nonempty, valid
PIN with `#`, both LEDs turn on for 150 ms, then return to the current stable
OPEN indication. Changes to OPEN during the flash are retained. Digits and
`*` do not flash the LEDs; empty or invalid submissions do not flash either.
A held `#` produces only one event. The flash acknowledges local entry, not
receiver acceptance: it also occurs if Wiegand is disabled or its queue is full.

Stopping clears the flash. OPEN continues to select the LED state while the
firmware waits for serial commands, without the normal debounce. Blocking
bench commands can defer LED updates until they return. OPEN does not actuate
the relay or authorize a PIN.

## Confirmation tones

Every debounced button press (digits, `*` and `#`) requests one beep; releases
and held keys do not repeat it. Key presses and RFID notifications share a queue. A tone lasts approximately
204 ms at 2.5 kHz; queued tones are separated by at least 20 ms of silence.
`BEEP DONE` reports completion. Rapid input can delay audible feedback because
tones are serialized. Up to 255 pending requests are retained; saturation
increments `TX_DROP`. Tones are generated by a timer, not a blocking delay.

## Commands and diagnostics

During continuous reading, only `!` is acted upon; other received characters
are discarded. With debugging off, send `!`, allow 500 ms for stopping, then
send `DEBUG ON` followed by a newline; `OK DEBUG ON` confirms access. Send
`READ` to resume with diagnostics. With debugging on, `!` replies `OK STOP`.
To return to silent operation, stop, send `DEBUG OFF`, then `READ`.
The debug setting is RAM-only and resets to OFF in the combined image.
Use line-oriented commands only while stopped. Stopping disables the carrier and tone, releases the rows, and clears
pending tones. Commands are case-insensitive ASCII terminated by CR, LF or
CRLF. Maximum command length is 15 characters.

| Command | Behavior while stopped |
| --- | --- |
| `DEBUG ON`, `DEBUG OFF` | Enable/disable UART diagnostics (OFF acknowledges before silencing) |
| `?` | Report firmware identity and GPIO/comparator configuration |
| `READ` | Start the combined reader with Manchester window sampling |
| `READPLL` | Start with the alternative edge-phase decoder |
| `READWIDTH` | Start with the alternative interval-width decoder |
| `READ16`, `READ32` | Bounded acquisition at 16 or 32 Âµs sampling intervals |
| `TRACE` | Collect a bounded qualified-edge trace after settling |
| `FIELD` | Generate the carrier for a bounded interval |
| `CAPTURE`, `LISTEN` | Capture raw transitions with or without carrier drive |
| `STOP` | Keep the carrier stopped |
| `WG26`, `WG34` | Select shared RFID/PIN frame length (RAM only) |
| `WGPROBE` | Test D0/D1 polarity through sense pins and enable on success |
| `WGOFF` | Disable output and put D0/D1 drivers in high-Z |
| `HELP` | List commands |

`READ16` and `READ32` retain the selected decoder. Bounded diagnostics are not
normal continuous operation. The separate `rfid_firmware_test` environment
supports the RFID commands without keypad scanning.

Periodic `READ` lines contain hexadecimal counters:

| Field | Meaning |
| --- | --- |
| `TIME` | Session elapsed time in 0.5 Âµs ticks, modulo 32 bits |
| `VALID` | Full valid frames since the previous report |
| `PREFIX` | Matching presence-prefix updates since the previous report |
| `RESETS` | Counted decoder resets since the previous report; not every internal resynchronization |
| `SAMPLE_MAX` | Largest observed sample gap in 0.5 Âµs ticks |
| `LATE`, `OVER` | Cumulative sampling-delay and FIFO-overflow faults |
| `WG_DROP` | Combined reader: cumulative refused Wiegand submissions (disabled or FIFO full) |
| `TX_DROP` | Cumulative dropped output diagnostics; also beep-queue saturation in the combined reader |

`DECODER=02` identifies window sampling, `01` edge-phase tracking and `00`
interval-width decoding. Output is diagnostic text, not a versioned binary
protocol. A UART consumer must tolerate status lines between input events.

## Main firmware bench verification — 2026-10-28

Firmware v1.2 was flashed to the STC8C2K32S2. The sense-pin polarity probe
returned `04 0C 08 0C`, confirming active-HIGH MCU output drive for both
Wiegand wires. At that test, the main build enabled Wiegand-26 at boot; hardware status
reported `WG BITS=26 ENABLED=1 ACTIVE=1`.

The user confirmed a beep per key, both LEDs flashing on PIN submission with
`#`, green with OPEN grounded and red when released. Serial capture recorded
`1234#` key events and beep completions, with no reported sampling-delay,
FIFO-overflow or Wiegand-drop faults. This confirmation does not establish
Arduino-side frame decoding or measured Wiegand pulse timing; those remain
unverified for this firmware revision.

Local evidence: `captures/firmware-v1.2/` (git-ignored).

### Scope verification — 2026-10-29

Flipper Zero emulated saved EM4100 tag `0102030405`. Rigol DS1104Z wiring
for this test was **CH1=green/D0, CH2=white/D1** (opposite the historical
factory-capture mapping). Both channels were exported as complete, aligned
6,000,000-sample records at 50 MS/s over 120 ms. Decoding used 2.5 V crossings.

| Mode | Decoded payload | Parity | Low pulse min/median/max | Falling-edge spacing min/median/max |
| --- | --- | --- | --- | --- |
| 26 | `030405` | Even/odd pass | 98.44 / 99.08 / 99.80 µs | 1712.04 / 1712.94 / 1713.78 µs |
| 34 | `02030405` | Even/odd pass | 97.12 / 99.08 / 101.76 µs | 1711.74 / 1713.02 / 1715.04 µs |

These match the expected low 24/32 tag bits. The captured frames contain
exactly 26 and 34 pulses respectively. Approximate high/low medians were
5.030/0.181 V on D0 and 4.973/0.137 V on D1, subject to physical probes matching
the scope's configured ×10 attenuation. Firmware reported no sampling-delay
or FIFO-overflow faults when stopping after the 34-bit test. This verifies
one RFID value in each mode; PIN frames and Arduino reception remain untested
on hardware. Reader was restored to running Wiegand-26; Flipper emulation stopped.

Evidence under `captures/wiegand/firmware-v1.2/` (git-ignored):
`acquisition-w4cb3int` (26-bit) and `acquisition-g_ujx94_` (34-bit), each with
manifest, raw channel files and `analysis.json`. `decode.py` records the
channel mapping and decoding calculation; `restored26.txt` records final state.

### PIN scope verification — 2026-10-29

The user entered `*1234#` while the scope was armed. With CH1=green/D0 and
CH2=white/D1, the capture contains exactly 26 pulses:
`00000000000000100110100100`. Payload `0004D2` is decimal 1234 (facility 0,
card 1234); leading even and trailing odd parity both pass.

Low pulse widths min/median/max: 97.12 / 99.08 / 101.12 µs. Falling-edge
spacing min/median/max: 1710.54 / 1712.50 / 1714.62 µs. Export retained two
complete aligned 6,000,000-sample channels at 50 MS/s, decoded at 2.5 V.
Evidence: `captures/wiegand/firmware-v1.2/acquisition-si1ndkvj/`, containing
manifest, raw channel files and `analysis.json` (git-ignored).
This verifies PIN 1234 in 26-bit mode on hardware; 34-bit PIN output and
Arduino-side reception remain unverified. The scope was left stopped on
this capture and reader configuration was unchanged.

On 2026-10-29, the source and main build defaults changed to Wiegand-34 for
both RFID and PIN output. `WG26` remains available as a runtime selection.

### 34-bit PIN scope verification — 2026-10-29

After selecting `WG34` and restarting reading, the user entered `*1234#`.
The capture contains exactly 34 pulses, bit string
`0000000000000000000000100110100100`, payload `000004D2` (decimal 1234).
Leading even and trailing odd parity both pass. CH1=green/D0; CH2=white/D1.

Low pulse widths min/median/max: 97.06 / 99.16 / 101.18 µs. Falling-edge
spacing min/median/max: 1711.26 / 1713.20 / 1715.96 µs. Both aligned channel
exports contain 6,000,000 samples at 50 MS/s; decoding used 2.5 V crossings.
Evidence: `captures/wiegand/firmware-v1.2/acquisition-v7kunnza/` (git-ignored),
including manifest, raw channels and `analysis.json`.

PIN 1234 has now passed hardware scope checks in both 26- and 34-bit modes.
Arduino-side reception remains unverified. Reader remains in runtime 34-bit
mode and the scope is stopped on this capture. The updated 34-bit power-on
default has been built and tested but has not yet been flashed.

### 34-bit RFID recheck after PIN test — 2026-10-29

Runtime status confirmed `WG BITS=34 ENABLED=1 ACTIVE=1`. A fresh Flipper
emulation of tag `0102030405` produced 34 bits, payload `02030405`, with both
parity checks passing. CH1=green/D0 and CH2=white/D1. Low pulse widths
min/median/max were 97.20 / 99.16 / 101.16 µs; falling-edge spacing was
1711.06 / 1712.94 / 1715.18 µs. Complete aligned raw records contain
6,000,000 samples per channel at 50 MS/s.

Evidence: `captures/wiegand/firmware-v1.2/acquisition-8akn0nm7/`, including
manifest, raw channels and `analysis.json` (git-ignored). Flipper emulation
was stopped afterward; the reader remains running in 34-bit mode.

### 34-bit default flashed and power-cycle verified — 2026-10-29

Flashed the main `keypad_reader` build with `K7612_WIEGAND_BITS=34` and
`K7612_WIEGAND_ACTIVE=1` to STC UID `F7DACA0C0A2524`. Programming completed
successfully. The post-flash status reported `WG BITS=34 ENABLED=1 ACTIVE=1`.
A separate RD6006 power cycle then confirmed the same setting in the unsolicited
cold-boot banner. No mode-selection or READ command was sent after that cycle.
Reading started automatically, with carrier on and zero LATE, OVER, TX_DROP
and WG_DROP counters in the observed startup reports. The device was left running.

Evidence under `captures/firmware-v1.2/`: `flash-default34.txt`,
`default34-after-flash.txt`, `default34-power-cycle.txt`,
`default34-cold-boot.txt` and `default34-image-sha256.txt` (git-ignored).
Programmed Intel HEX SHA-256: `8947d6fbc680f028cefcd77bd78c5d575a9387896bb5a0a7c44d4f8a3f7f79ed`.

### Post-power-cycle RFID waveform confirmation — 2026-10-29

Following the verified cold boot of the 34-bit-default image, no commands
were sent to the reader during this check. Flipper tag `0102030405` triggered
a fresh scope acquisition with CH1=green/D0 and CH2=white/D1. The complete
frame has 34 bits, payload `02030405`, and passing even/odd parity.
Low pulses min/median/max: 97.06 / 99.10 / 101.12 µs; falling-edge spacing:
1709.18 / 1711.60 / 1713.54 µs. Both aligned channels contain 6,000,000 samples
at 50 MS/s; analysis uses 2.5 V crossings.

This confirms actual 34-bit RFID output after the power cycle, independently
of the UART configuration report. Evidence is in
`captures/wiegand/firmware-v1.2/acquisition-qqt13_hh/` (manifest, raw channels,
`analysis.json`; git-ignored). Flipper emulation was stopped; reader remains
running and scope remains stopped on this capture.


## Silent-default bench verification — 29 October 2026

Flashed the final combined image (14,635 bytes; STC UID `F7DACA0C0A2524`),
including the explicit UART RX pull-up. Firmware HEX SHA-256:
`33620d42e7f19951df52f0c3a1b58144838e5f21d5e8d3d25c378ec7d88ba8f6`.

After a cold power cycle, passive UART monitoring at 9600 8N1 received zero
bytes from power-on through approximately 138 seconds of operation. No UART
commands were sent. One null byte was recorded during the power-off interval,
before power-on, and is retained separately in the evidence.

Flipper EM4100 emulation of `0102030405` produced two captured Wiegand-34 frames,
one on initial presentation and one after removal/re-presentation. Both carried
`02030405` with valid leading even and trailing odd parity. A rearmed scope
remained waiting during the held-tag observation (at least 30 seconds), then
triggered on re-presentation. Pulse widths across both records were 97.16–101.22
µs, falling-edge spacing 1.7095–1.71456 ms, and frame durations 56.58–56.60 ms.

The user confirmed this session's probes as **CH1 = green D0, CH2 = white D1**,
the reverse of the historical factory-test channel mapping. Scope attenuation
was configured as ×10. Complete aligned 6-million-point captures per channel,
decoding, passive UART events, power timestamps, flashed HEX and result summary
are saved locally under `captures/final-silent-20261028T214457Z/` (UTC directory
name). Final state: reader running with debugging OFF; Flipper emulation OFF.
This test verifies silent startup and RFID-to-Wiegand output; it does not newly
validate keypad or OPEN input behavior on hardware.

### ESPHome frame-gap regression update - 29 October 2026

The scheduler's minimum idle between frames was increased from 50 ms to 150 ms.
The earlier physical captures and firmware hash above describe the previous
build; they do not constitute a hardware test of this change. Pulse widths,
within-frame bit spacing, payload encoding and the two-frame FIFO are unchanged.

The SDCC/s51 regression exercises two queued frames in all eight combinations
of 26/34-bit format, output polarity and 16/32-microsecond sampling. It measures
pin transitions, checks the 150 ms minimum with less than one sampling interval
of rounding, and verifies that a separate 100 ms idle-time receiver model emits
two complete frames. Actual ESPHome reception under Wi-Fi/API load remains to
be tested; no finite gap guarantees reception if the receiver loop is stalled.


### 150 ms frame-gap hardware verification - 29 October 2026

Flashed the updated 14,635-byte image, SHA-256
`a1f62b152106c479869b32888a36a96446b6875aa0bc34db3f9b3d3115a2f43e`,
to STC UID `F7DACA0C0A2524`. Flipper CLI emulation alternated synthetic EM4100
IDs `0102030405` and `0102030406` ten times in approximately two seconds.
Different valid IDs request separate tones, so this intentionally produced a
burst of queued beeps. Presence expiry and tone timing were not changed.

Rigol capture used CH1=D0 and CH2=D1, 2.5 V edge thresholds, a 2.4-second
record and 0.4-microsecond sample spacing. Six complete Wiegand-34 frames were
captured; all had valid parity and expected payloads `02030405` or `02030406`.
Idle gaps, measured from final pulse release to the next frame's first falling
edge, were 151.8176, 149.9900, 449.8456, 150.0520 and 404.2168 ms. The shortest
gaps agree with the nominal 150 ms scheduler setting within hardware clock and
edge-timing variation; this is not a claim of an absolute 150.000 ms minimum
under all oscillator conditions. Pulse widths were 97.2-101.6 microseconds,
and within-frame falling-edge spacing was 1.7100-1.7148 ms.

The burst yielded six captured frames from ten emulation intervals. This does
not establish lossless acquisition or queue delivery at that input rate; with
diagnostics OFF the capture cannot attribute missed submissions to RF decoding
versus queue refusal. UART monitoring received zero bytes during the stimulus.
No changes to reading mode or debug settings were sent. Reader remains running
with debugging OFF, and Flipper CLI emulation was explicitly stopped.

Local evidence: `captures/wiegand-gap150-20261028T220738Z/`, including flashed
HEX, stimulus log, analysis, result JSON and `acquisition-806mrnx1/` full-memory
capture. This verifies the STC output gap with Flipper/Rigol; ESP32 reception
under Wi-Fi/API load still requires a separate integration test.

A portable [measurement summary](bench-results/wiegand-gap150-2026-10-29.json)
is included in the repository; raw acquisition files remain local.
