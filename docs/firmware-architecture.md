# Firmware architecture

The firmware targets the STC8C2K32S2 at 24 MHz with 32 KiB program flash,
2 KiB XRAM and 256 bytes internal RAM. State is fixed-size throughout;
there is no heap allocation or operating system.

## Source layout

| File | Responsibility |
| --- | --- |
| `src/keypad_reader.c` | Combined entry point enabling keypad support |
| `src/rfid_firmware_test.c` | Shared hardware setup, timer ISRs, UART and service loop |
| `src/keypad_service.h` | Cooperative scan, debounce and shared tone scheduling |
| `src/rfid_sampler.h` | Qualified comparator edges and ISR-to-foreground FIFO |
| `src/rfid_decoder.h` | Frame/parity validation and selectable decoders |
| `src/rfid_header_sampler.h` | Default Manchester window sampler |
| `src/rfid_presence.h` | Notification suppression and absence expiry |
| `src/rfid_capture.h` | Bounded raw-capture diagnostics |

The combined entry point includes the shared implementation with
`K7612_KEYPAD` defined. PlatformIO compiles one entry point per environment;
the shared C file must not also be compiled separately in the combined target.

## Hardware ownership

| Resource | Owner |
| --- | --- |
| Timer0 / P3.5 T0CLKO | Nominal 125 kHz carrier |
| Timer1 | High-priority comparator sampling every 16 µs during normal reading |
| Timer2 | UART baud generation during reading |
| Timer3 | Free-running 0.5 µs timestamps |
| Timer4 / P3.4 | Confirmation tone, 200 µs half-period |
| Comparator P3.6/P3.7 | RF demodulator input |
| P0.0–P0.3, P1.5/P1.4/P1.3 | Open-drain matrix rows and input columns |

The comparator uses the external input pair. Its physical output is disabled
because that pin is the buzzer drive. The firmware imposes no pull-up bias on
the analog inputs. Relay, LED and Wiegand functionality is not part of the
combined reader.

Boot uses Timer1 for UART initialization, verifies the Timer2 UART path, then
reassigns Timer1 to sampling. Startup probes are bounded and leave the carrier
off on failure. The carrier remains active while reading; factory-style idle
duty cycling is not implemented.

## Acquisition and concurrency

The sampler qualifies a transition after at least 24 µs of stability
(nominally 32 µs at the default cadence). It timestamps the initial observation
rather than qualification completion. The FIFO holds 127 events with a 16-bit
timestamp and the level preceding the transition.

The sampling ISR does not decode frames, format UART messages or inspect
presence. A gap above 24 µs or a full FIFO raises a fault. Foreground handling
discards uncertain queued data, resets decoding and postpones expiry of an
already-present tag: lost samples are not proof of removal.

UART transmission is buffered and tones use a separate timer. The keypad
scanner performs one row transition or sample per service interval, allowing
at least 1 ms for each released/selected phase. It does not borrow the carrier
timer or wait for settling. Diagnostics reserve UART space before writing;
overload is counted instead of stalling acquisition.

The sampling ISR uses register bank 1; the tone ISR uses bank 2. The sampler
helper disables SDCC parameter/local overlaying because register banks do not
protect overlaid RAM. The regression suite interrupts foreground clock work
with the actual compiled sampler ISR to check this boundary.

## Manchester decoding

The default decoder assumes EM4100 RF/64: a 512 µs bit comprising two 256 µs
half-bits. Four bit phases, spaced 128 µs apart, integrate the central 128 µs
of each half-bit using qualified edge timestamps. The lower-disagreement
Manchester interpretation selects each bit; equal costs reset assembly.

Each phase searches for a nine-one header with independent polarity candidates,
preventing long zero payloads from blocking the real header. Acceptance requires:

1. Nine leading one bits.
2. Ten rows of four data bits with even row parity.
3. Four column-parity bits covering the ten data rows.
4. A zero stop bit.
5. At most 6.25% disagreement with the selected Manchester waveform across
   the evaluated central windows.

Parity never changes a decoded bit. The disagreement limit is an engineering
bound, not a calibrated false-accept probability. Decoding is payload-independent
and preserves the full five-byte identifier.

A complete valid frame selects the sampling phase to reduce foreground work.
After 128 bit decisions without another valid frame, phase search resumes.
A gap over 1 ms also resets alignment. Duplicate publications from neighboring
phases are suppressed within 2 ms using a saturating elapsed counter, avoiding
aliasing at the 16-bit timestamp wrap.

Each transition processes at most four phase states and eight window boundaries
per phase before returning. Mutually exclusive decoder modes share candidate
storage. Alternative edge-phase and width decoders remain available for
diagnosis; they are not combined to repair a rejected frame.

## Presence and feedback

Acquisition requires a complete valid frame. Matching valid frames refresh
presence. A matching 32-bit prefix may refresh an already-confirmed, unexpired
tag, but cannot acquire or notify a tag. This preserves suppression through
short full-frame dropouts; it is not authentication and distinct tags can
share a prefix.

After 1.5 seconds without eligible evidence, presence expires. A subsequent
full valid acquisition can notify again; a different valid identifier notifies
immediately. Key and RFID tones share a queue, with no repeat notifications
for a held key or tag.

## Resource budget

The SDCC 4.4.0 combined build uses approximately 11 KiB flash and 1.9 KiB XRAM.
The linker reports roughly 130 bytes available for stack. These are build
figures, not measured worst-case stack use; inspect the generated `firmware.mem`
after changes. Direct-addressable internal RAM is particularly constrained.

## Validation and limits

Run `python tools/test_reader.py` after building the toolchain. Tests generate
their own fixtures and compile actual firmware with SDCC. Coverage includes
multiple identifiers and polarities, alignment, parity, stop bits, sampled
glitches, delayed foreground work, timestamp wrap, debounce, held-key suppression,
multi-key handling, tone queues, presence expiry and asynchronous ISR storage
isolation. Combined streaming tests scan keys while decoding RFID samples.

The generic simulator cannot validate STC timer behavior, analog reception,
electrical levels or real-time margins. Hardware validation covers RFID
presentation/removal, key presses and a held key, with confirmation tones and
no reported sampling or FIFO faults in the completed checks. It does not
establish guaranteed reading range or broad board compatibility.

RF/32, arbitrary bit rates, large clock drift and other modulation formats are
unsupported. Poor analog signals can still lose complete frames. Presence-prefix
matching trades stronger held-tag suppression for weaker evidence of continued
identity. Applications requiring credential authentication or safety-critical
access control need design and validation beyond this reader.
