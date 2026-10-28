# Reader interface

The default `keypad_reader` environment is the main K7612 firmware. It combines
keypad scanning, EM4100 reading, Wiegand output, button-press tones and OPEN-driven
LED indication. Services start automatically on power-on. UART is 9600 baud,
8 data bits, no parity and one stop bit, without flow control. UART debugging is
OFF after every power-on/reset: no banner, probe byte, events or periodic reports
are transmitted. Wiegand, OPEN LEDs, keypad and tones operate without a UART host.
The event descriptions below apply when debugging has been explicitly enabled.

See the [hardware pin map](../README.md#hardware) for MCU connections.
D0/D1 carry Wiegand output; OPEN controls LED indication. The main firmware
does not control the relay.

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
bits; trailing odd parity covers the last 12/16.
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
P3.2/D1. Sense inputs are high impedance with no internal pull-ups. The main PlatformIO build sets
`K7612_WIEGAND_ACTIVE=1` and enables Wiegand-34 automatically at boot.
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
pulse. This leaves nominal margin beyond ESPHome's 100 ms receive timeout. STOP
releases an active pulse immediately and discards pending frames/PINs;
the receiver may see a partial frame if stopped during transmission.

## OPEN and LED feedback

Green is mapped to P2.6 and red to P2.7. OPEN is a high-impedance input on
P2.2 with internal pull-ups disabled. Use an external transistor to pull OPEN
LOW, as described in the [ESPHome connection guide](esphome-connection.md).

| OPEN | Steady indication |
| --- | --- |
| HIGH (released) | Green on, red off |
| LOW (grounded) | Red on, green off |

The initial state is displayed at boot. During normal operation, a changed
OPEN level must remain stable for 20 ms. On submission of a nonempty, valid
PIN with `#`, both LED pins go HIGH for 150 ms, then return to the current stable
OPEN indication. Changes to OPEN during the feedback interval are retained. Digits and
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
| `READ16`, `READ32` | Bounded acquisition at 16 or 32 µs sampling intervals |
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
| `TIME` | Session elapsed time in 0.5 µs ticks, modulo 32 bits |
| `VALID` | Full valid frames since the previous report |
| `PREFIX` | Matching presence-prefix updates since the previous report |
| `RESETS` | Counted decoder resets since the previous report; not every internal resynchronization |
| `SAMPLE_MAX` | Largest observed sample gap in 0.5 µs ticks |
| `LATE`, `OVER` | Cumulative sampling-delay and FIFO-overflow faults |
| `WG_DROP` | Combined reader: cumulative refused Wiegand submissions (disabled or FIFO full) |
| `TX_DROP` | Cumulative dropped output diagnostics; also beep-queue saturation in the combined reader |

`DECODER=02` identifies window sampling, `01` edge-phase tracking and `00`
interval-width decoding. Output is diagnostic text, not a versioned binary
protocol. A UART consumer must tolerate status lines between input events.
