# K7612 RFID reader firmware design

The firmware goal is an **ongoing, payload-independent EM4100 reader on the
STC8C2K32S2**: generate the RFID carrier, receive modulation through the onboard
comparator, recover Manchester bits, validate complete frames, and report
five-byte tag IDs while continuing to listen. `0102030405` is the reference
test vector; the decoder must accept any valid payload.

## Reader operation

Use the measured factory idle cadence as the starting point for carrier control:
approximately 7.16 ms of 125 kHz drive on P3.5 every 100 ms with no tag present.
During an active receive interval, acquire and decode the P3.6/P3.7 signal.
A tag read requires extending carrier-on time beyond an idle probe burst; the
tag capture contains at least 119.96 ms of uninterrupted carrier. The transition capture shows modulation within roughly 1 ms of startup and
continued carrier before the next frame header. This supports activity-based
extension; the exact qualification threshold remains an implementation choice. After a valid read, keep the reader
service running, including any carrier-off interval and subsequent restart. A stationary
tag can transmit repeated frames even when the application emits only one
notification for its presence.

The receive lifecycle is:

1. **Start and settle:** enable the carrier and discard or qualify startup
   transitions for an interval determined on hardware.
2. **Acquire:** recover timing, Manchester phase and polarity from qualified
   comparator transitions; search for frame headers.
3. **Validate:** collect a complete frame and check header, row parity, column
   parity and stop bit. Reject corrupt candidates and reacquire automatically.
4. **Report and continue:** emit each complete candidate frame as hex. If its
   framing and parity checks pass, also emit its five-byte data. Continue the
   reader lifecycle. A valid acquisition also schedules the confirmation beep
   specified below; repeated frames do not restart or overlap that beep.
5. **Recover or stop:** loss of modulation, malformed timing or queue overflow
   resets decoder synchronization without ending the reader session. An explicit
   stop or hardware fault disables the carrier and holds P3.5 low.

Use **the first rising edge of the extended carrier burst as t = 0** for
startup, modulation, frame and shutdown timing. Measure off intervals from
the final falling edge of the preceding burst. CH4 only triggers acquisition;
it is not the timing origin for reader behavior. There is no measured basis
for a one-second read limit.

The no-tag acquisition directly establishes that the factory reader duty-cycles
the carrier: approximately 7.16 ms on and 92.83 ms low, repeating every 100 ms.
Implement carrier control separately from frame decoding, with idle probing,
extended reception and a post-read off interval. The transition capture measures a 134.55 ms active read, followed by
a 185.414 ms carrier-off interval and restart. Receive modulation precedes the
normal idle cutoff and the next header, supporting extension on activity. One
record establishes this sequence, not an invariant post-read delay.

Keep acquisition and decoding responsive alongside keypad and UART work.
Authorization, relay control, buzzer feedback, duplicate suppression and tag
absence are application policies consuming decoder results. Absence timeouts
must account for observed receive dropouts; a single bad frame is not removal.

## Evidence and implementation status

| Component | Established status |
| --- | --- |
| Factory carrier | P3.5 measured at 125 kHz; no-tag bursts of about 7.16 ms every 100 ms |
| Analog receive data | Three complete parity-valid `0102030405` frames recovered from P3.6 minus P3.7 |
| Offline analysis | Known-payload-assisted timing/polarity fit followed by measured-bit recovery and parity checks |
| Embedded code currently available | Carrier generation and bounded, polled comparator captures in `src/rfid_firmware_test.c` |
| Embedded work required | Hardware-verified comparator reception, streaming Manchester decoder, frame validation and ongoing read reporting |

The analog capture establishes that the board exposes recoverable tag data.
The replacement image's carrier and comparator settings still need hardware
verification. The timed commands in the appendix are tools for that verification;
their durations and edge buffer are not the reader architecture.

## Hardware interface

| MCU pin | Physical pin | Traced connection | Reader role |
| --- | --- | --- | --- |
| P3.5 | 18 | R17 -> R20 -> Q3 base; Q3 emitter grounded | T0CLKO carrier output, nominal 125 kHz |
| P3.6 | 19 | R23 -> capacitor/diode network | External comparator input |
| P3.7 | 20 | C19 -> P3.6; no other connection found | Other external comparator input |
| P3.4 | 17 | Buzzer drive | Acquisition confirmation beep; also used as scope trigger; never route comparator output here |

Q3 collector, Q2 emitter, Q5 emitter, and coil red were traced to a shared
node. Q5 collector is grounded. Q1/Q2 collectors share another node, distinct
from the 47 uF capacitor-positive/Q1-base node. Q1 emitter reaches Q2 base
through R18; R19 joins Q2 and Q5 bases. The other coil wire enters a
capacitor/diode/resistor network. These are bench observations, not a complete
verified schematic. In particular, P3.7's DC bias and C19's function are unknown.

The [STC8C manual](https://www.stcmicro.com/datasheet/STC8C-cn.pdf), chapters
12, 13 and 15, documents the timer clock output, Timer2 and comparator.
The comparator chapter and examples identify P3.6 as CMP- and P3.7 as CMP+.
With the external pair selected, CMPRES=1 means P3.7 exceeds P3.6, opposite
to the sign of the plotted P3.6 minus P3.7 difference. Verify the resulting
filtered digital waveform on hardware. Keep physical comparator output routing disabled so it cannot
drive the buzzer. The diagnostic register settings are listed in the appendix.

## EM4100 wire format for the complete decoder

Initial implementation target: standard **Manchester RF/64** only. At 125 kHz,
one bit is 64 carrier cycles = 512 µs, and one half-bit is 32 cycles = 256 µs.
Other EM4100 rate variants exist; do not silently interpret them as RF/64.

One repeated frame has 64 bits, transmitted in this order:

1. Nine header bits, all `1`.
2. Ten rows, each containing a four-bit payload nibble **MSB first**, followed
   by one **even row-parity** bit. The high nibble of each byte precedes the
   low nibble, with bytes in the supplied five-byte order.
3. Four **even column-parity** bits, one for each payload-bit column across
   the ten rows. Row-parity bits are excluded from column parity.
4. One stop bit, `0`.

For `0102030405`, including header/parity/stop, the frame is:

```text
Frame hex: FF8060280C048142
Bits: 1111111110000000011000000010100000001100000001001000000101000010
```

Flipper's encoder emits each bit's level for the first half, then its inverse
for the second. This describes the encoder convention, not necessarily the
voltage or CMPRES polarity at the reader. Manchester has a transition in every
bit center; transitions at bit boundaries depend on adjacent data bits.
Normal inter-edge intervals are consequently about **256 or 512 µs**.

Sources: [Flipper EM4100 implementation](https://github.com/flipperdevices/flipperzero-firmware/blob/dev/lib/lfrfid/protocols/protocol_em4100.c)
and [EM Microelectronic EM4200 datasheet, EM4100-compatible framing](https://www.emmicroelectronic.com/sites/default/files/products/datasheets/em4200_ds.pdf).

## Decoder implementation

### 1. Comparator setup and software filtering

Use P3.7 as CMP+ and P3.6 as CMP-, configured as high-impedance inputs with
pull-ups disabled. **CMPRES=1 means P3.7 > P3.6**, opposite to the sign of the
plotted P3.6 minus P3.7 difference. Read the result with `CMPCR1 & 1`.

| Setting | Purpose |
| --- | --- |
| `CMPCR1 = 0x84` | Comparator enabled, external input pair, edge interrupts and physical output disabled |
| `CMPCR2 = 0x10` | Initial filter setting: 0.1 µs analog glitch filter enabled and 0.75 µs digital qualification at 24 MHz |

Keep physical comparator output disabled (`CMPOE`, bit 1): P3.4 belongs to the
buzzer. Keep comparator edge interrupts disabled for the sampled receive path.
The hardware digital filter requires a stable new level for `(LCDTY + 2)`
system clocks when LCDTY is nonzero; zero disables it. Its maximum setting,
63, gives **2.708 µs at 24 MHz**.

**Programmable voltage hysteresis is not documented for the STC8C2K32S2;
treat it as unavailable.** Do not import newer STC variants' `CMPEXCFG`
hysteresis settings into this implementation.

Implement the receive path as:

1. Sample CMPRES at a fixed rate covering multiple phases of the 8 µs carrier.
2. Maintain a rolling high-sample count over approximately **32 µs** initially.
   Add the incoming bit and subtract the outgoing bit; no division is needed.
3. Apply separate rising and falling count thresholds for **software
   hysteresis**, then timestamp the resulting transitions for Manchester decoding.
4. Reset sample history on carrier startup and qualify activity before extending
   the idle probe; do not interpret the startup transient as tag data.

Sample rate, window and thresholds are development parameters to verify against
the actual CMPRES signal. For example, 2 µs sampling covers four carrier phases
and gives 16 samples per window, but allows only **48 system clocks per sample**
at 24 MHz. Check generated instruction timing and measured service deadlines
before selecting that rate. Include filter latency and sampling jitter in the
Manchester timing tolerance. Software averaging of comparator bits measures
high-state occupancy; verify that it preserves the modulation seen in the analog
capture. Set startup blanking and threshold margins from measured behavior.

The existing diagnostic writes `P3IE` at 0xFE33. ELL's STC8C header defines this
address, but the reviewed STC8C manual lists only P0IE/P1IE. Keep that write out
of the baseline until support is resolved; high-impedance mode and P3PU pull-up
control are documented independently.

Sources: [STC8C manual, §§9 and 15](https://www.stcmicro.com/datasheet/STC8C-cn.pdf)
and [ELL comparator implementation](https://github.com/gevico/mcs51-ell/blob/745f2b7df179248f12ffea24230a53e1ea628034/libraries/peripherals/STC8x/src/STC8x_COMP.c).
The [ELL STC8C header](https://github.com/gevico/mcs51-ell/blob/745f2b7df179248f12ffea24230a53e1ea628034/libraries/core/register/STC8x/STC8Cx_REG.h)
is the conflicting P3IE reference. These establish peripheral setup, not a
validated K7612 software filter. The compact
[rfrw decoder](https://github.com/c-not-around/rfrw/blob/82bcd96fb09dfcb59cfb690014c267f9d99ca211/hardware/PIC12F1840/firmware/rfid/rfid.c)
is a useful reference for downstream edge timing and EM4100 framing.

### 2. Stream reception with bounded memory

Budget for up to two transitions per Manchester bit, approximately 3,906
transitions per second at RF/64, plus any residual noise. Size the event queue
for the maximum measured service delay and consume it continuously.

Use a bounded event queue plus a streaming Manchester/framing state machine,
or process timestamped transitions directly with a measured service budget.
Store a 64-bit candidate frame, decoder state, and counters rather than an
unbounded waveform. Queue overflow must invalidate synchronization and be
reported. Feed this stage with software-filtered transitions. Measure sampling
and decoder service deadlines with the eventual keypad/UART workload. The
existing keypad `tick_1ms()` reconfigures Timer0 and must be replaced with
nonblocking scheduling; Timer0 is reserved for carrier generation.

Timer2's current 0.5 µs ticks give nominal intervals of **512 ticks per
half-bit** and **1,024 ticks per bit**. Its 16-bit wrap occurs at **32.768 ms**,
exactly one nominal frame. Retain wrap-safe elapsed-time accounting; do not
reset or infer frame position from timer rollover. If an event-only scheme
replaces continuous polling, add overflow accounting or a periodic timebase
service so long idle gaps cannot masquerade as short valid intervals.

Keep UART formatting/transmission out of time-critical acquisition paths.
At 9600 baud, verbose edge dumps cannot keep up. Report completed frames and
compact statistics, with detailed dumps reserved for stopped diagnostics.

### 3. Synchronize and validate independently of the known payload

- Classify qualified intervals around one or two half-bits. Derive tolerance
  bands from measured jitter and filter delay; begin with configurable bands
  around nominal timing, never accept arbitrary long gaps as valid bits.
- Acquire Manchester phase, track mid-bit versus boundary transitions, and
  resynchronize after invalid intervals or missing edges. Handle either input
  polarity during acquisition, then retain the validated interpretation.
- Search decoded bits for the nine-one header, collect the remaining frame,
  check all ten row parities, all four column parities, and the zero stop bit.
  On rejection, resume searching without assuming the next edge is a header.
- Extract exactly five bytes with leading zeros preserved. No comparison to
  `0102030405` may be required for decoding; it is a test vector only.
- Report each complete candidate and its validation result through the output
  contract below. One valid frame is sufficient to print its data; do not wait
  for a matching repetition. Keep tag absence and authorization/lock control
  separate from decoder output.

For a repeated frame stream with unknown initial phase, allow nearly one frame
to reach the next header plus one complete frame: approximately **65.536 ms**,
before startup/filter margin. Extend the initial probe on qualified activity
so reception can reach a complete frame. Return to the reader lifecycle after
acquisition; determine timeout margin from measured behavior.

### UART output contract

Use UART1 at 9600 baud, 8N1, with ASCII lines terminated by CRLF. For every
complete 64-bit candidate collected after header synchronization, print its
entire frame as **16 uppercase hexadecimal digits**, preserving leading zeros
and packing bits in transmission order (the first received bit is bit 63).
The frame includes header, payload, row parity, column parity and stop bit.

If the nine-one header, all ten even row parities, all four even column parities
and the zero stop bit pass, immediately follow it with the five-byte payload
as **10 uppercase hexadecimal digits**, preserving leading zeros:

```text
Frame hex: FF8060280C048142
Data: 0102030405
```

EM4100 uses parity checks rather than a separate checksum field. On validation
failure, print only the `Frame hex:` line; omit `Data:`. A partial frame or loss
of Manchester synchronization does not constitute a complete frame to print.
Report repeated complete frames individually for now, with no duplicate
suppression and no requirement for two matching frames. Keep each frame/data
pair together and in receive order.

Queue output without blocking receive timing. A valid pair is 47 bytes including
CRLF, taking about 49 ms at 9600 baud, while frames can repeat every 32.768 ms.
Consequently, indefinitely reporting every repeated frame cannot be sustained
at this baud rate. Buffer the bounded receive burst and drain pending output
during the carrier-off interval before starting the next probe. Bound the
receive burst by available output capacity; stop acquisition cleanly before
capacity is exhausted rather than silently dropping a completed frame. This is
a development reporting policy, not an inferred factory timing requirement.

### Acquisition confirmation beep

After a complete frame passes all framing and parity checks, end that acquisition
by disabling the carrier and holding P3.5 low, then beep once on **P3.4**.
Do not beep for incomplete or invalid frames. Emit the UART frame/data pair as
specified above; UART transmission must not delay or alter beep timing.

The transition acquisition measures this output relative to carrier transitions:

| Measurement | Observed value |
| --- | --- |
| P3.4 idle and final state | High |
| Carrier cutoff to first buzzer falling edge | 129.2 µs |
| Median falling-edge period | 400 µs (2.5 kHz) |
| Median interval between alternating edges | 200 µs |
| Low pulses in the beep | 255 |
| First falling edge to final rising edge | 101.6152 ms |

Use **130 µs after carrier cutoff**, **200 µs low / 200 µs high**, and
**255 low pulses** as nominal implementation settings. Begin with a falling
edge and leave P3.4 high after the last pulse. With exact 200 µs half-periods,
the last rising edge occurs 101.8 ms after the first falling edge; the measured
101.6152 ms is the actual capture span, not a reason to change the nominal
2.5 kHz tone. An independent tag capture also shows a 2.5 kHz beep, with a
162.32 µs cutoff-to-beep delay, so the short delay is not an invariant constant.

Schedule the waveform from a timer/timebase without blocking UART or keypad
service. Timer0 remains reserved for carrier generation; do not reconfigure its
clock-output routing onto P3.4. Configure P3.4 as a controlled GPIO output with
an idle-high latch before enabling its output driver, and keep comparator
physical output routing disabled. Verify the output mode and waveform on the
development unit.

Beep once per successful acquisition, not once per repeated frame already
buffered during that acquisition. Do not overlap or restart an active beep.
Hold the carrier off during the beep and while draining the bounded UART output,
then resume probing. Persistent-tag duplicate suppression remains a separate
policy; this initial rule does not claim to reproduce the factory's removal
or re-presentation detection.

## Acceptance checks

Required software cases include both Manchester polarities; arbitrary initial
frame phase; all-zero/all-one and varying payloads; RF/64 timing drift and
jitter; bad row/column parity; bad header/stop; truncated frames; missing/extra
edges; short glitches; queue overflow; timer wrap; and reacquisition after tag
removal. Rejected frames must never emit an authorized/unlock event.

Use the full-memory record as a regression fixture, but also generate signals
independently of the decoder and test other payloads. The current analysis tests
cover framing construction, moving-average timing, aliasing/alignment rejection,
and known-template fitting with inverted synthetic polarity:

```powershell
.\.venv\Scripts\python.exe tools/test_clean_rfid_capture.py
.\.venv\Scripts\python.exe tools/test_overlay_em4100.py
.\.venv\Scripts\python.exe tools/test_analyze_rfid_acquisition.py
```

On hardware, compare factory and replacement carrier levels and analog inputs;
verify CMPRES polarity/filtering; repeat with no tag, fresh presentation,
stationary emulation, removal/re-presentation, and a physical compatible tag.
Then enable normal keypad/UART activity and measure missed edges, queue usage,
and timing jitter again. Confirm startup, explicit stop, fault handling,
invalid-frame recovery and reacquisition. Verify that the reader service continues beyond one second,
that repeated valid frames are decoded during active reception, and that
notification suppression does not prevent subsequent reads. Verify carrier
restart and decoder reacquisition after any intentional off interval. Confirm carrier-off behavior when the reader is disabled.

Verify one confirmation beep per valid acquisition, no beep on invalid frames,
2.5 kHz output for approximately 102 ms, idle-high P3.4, and the short delay
from carrier cutoff independently of UART traffic.

Completion means a **payload-independent, parity-checked reader running on the
STC**, repeatedly reporting correct five-byte IDs with bounded resource use and
no false reads in no-tag tests. Offline recovery is evidence for that design,
not completion of the embedded decoder or authorization logic.

## Reference measurements and analysis

### Measurement setup

Reference configuration: K7612 factory firmware and a **Flipper Zero emulating
standard EM4100 with payload `01 02 03 04 05`**. Physical-tag compatibility
requires separate validation.

Probe mapping:

| Scope channel | MCU signal | Measurement |
| --- | --- | --- |
| CH1 | P3.7, pin 20 | Comparator input |
| CH2 | P3.6, pin 19 | Comparator input |
| CH3 | P3.5, pin 18 / MCU side of R17 | Carrier drive |
| CH4 | P3.4, pin 17 | Buzzer drive |

All probes used ×10 attenuation and DC coupling, with grounds on board GND.
Use connected component pads where possible to avoid bridging adjacent MCU pins.
The trigger was CH4 falling through 2.5 V, armed with the tag away before
presentation. **The buzzer only triggers the scope capture; it carries no RFID
data and is not the reference for decoder timing.** Rebase recorded timestamps
on the relevant CH3 carrier transition. Use carrier startup when present; for
a record lacking startup, explicitly use carrier cutoff instead. Never infer
an unrecorded startup time from the capture trigger.

### Signal characteristics

- P3.5 has approximately 0–5 V carrier drive, **8 µs period / 125 kHz**, with
  approximately 4 µs high and 4 µs low during steady operation.
- CH3 has 29,993 threshold crossings at 2.5 V, with a median edge interval of
  **4 µs** and a maximum of **4.08 µs**: no missing carrier cycles are visible
  between the first and last recorded edges.
- For `acquisition-41akvfzx`, startup is outside the record. Define **t = 0
  at carrier cutoff**, the final CH3 falling edge, for this dataset only.
- P3.5 stays low until the record ends **120.04232 ms after cutoff**, with no
  restart. The observed active portion is about **119.96 ms**, a lower bound
  on total carrier-on time.
- The last complete validated frame ends **12.50964 ms before cutoff**.
  Cutoff is approximately 24.43 bits into the next repetition at the fitted
  timing; it does not coincide with that stream's frame boundary. This does
  not identify which frame the factory firmware accepted or its cutoff rule.
- P3.4 produces approximately **2.5 kHz** buzzer drive (200 µs half-period).
- The full record contains receive modulation throughout the observed
  active-carrier interval. It does not include the start of that extended read, so
  its complete duration and initial settling cannot be measured from it.
- Three complete repeated frames are present before recognition.
- No-tag carrier cadence and a post-read restart are measured in the
  acquisitions below. Repeated-read notification policy is not established.

The receive information is clearest in **CH2 − CH1 = P3.6 − P3.7**. Both pins
contain carrier-related ripple; looking at either pin alone does not isolate
the differential receive signal.

Offline processing used a centered **32 µs moving average**, four carrier
periods, on the voltage difference, followed by decimation to about 4 µs
spacing. The resulting envelope changes sign with Manchester modulation.
The filtered modulation is on the order of tens of millivolts and varies
through the read. Derive receiver thresholds from comparator characterization.

### No-tag carrier acquisition

With the keypad powered, no tag present and CH3 still connected to P3.5, a
fresh single acquisition at 100 ms/div recorded **1.2 s** at **2.5 MS/s**
(400 ns sample spacing, 3,000,000 raw samples). CH3 was DC coupled with a ×10
probe; the trigger was CH3 rising through 2.5 V.

Raw threshold crossings show **12 complete bursts**, followed by the beginning
of a thirteenth at the record boundary:

| Measurement | Result |
| --- | --- |
| Carrier edge interval within bursts | Median 4 µs, consistent with 125 kHz |
| First rising to final falling edge per complete burst | About 7.1632 ms |
| Burst start-to-start interval | About 99.990 ms |
| Low interval between bursts | About 92.827 ms |
| Complete cycles per burst | 896 (1,792 edges) |
| Carrier duty across the idle cycle | About 7.16% |

This directly establishes periodic carrier shutdown **without a tag**. Each
idle burst is shorter than one RF/64 frame (32.768 ms); a complete decode
therefore requires a longer active interval, as observed in the tag acquisition.

### Transition from idle probing to extended reception

`captures/acquisition-v5n6763o/manifest.json` records all four channels at
2.5 MS/s over 1.2 s. The tag was presented until the beep, then removed.
**t = 0 is the first rising edge of the extended carrier burst.** The record
covers −865.3200 to +334.6796 ms on this carrier-relative axis:

| Event | Time relative to extended carrier startup |
| --- | --- |
| Last short probe | −99.9896 to −92.8100 ms (7.1796 ms) |
| Extended carrier starts | 0 ms |
| Normal idle cutoff would occur | +7.1632 ms |
| First complete fitted frame begins | +23.7238 ms |
| Carrier stops low | +134.5500 ms |
| Carrier restarts | +319.9640 ms |

The preceding short probes show a startup transient followed by a nearly flat
filtered receive difference. The extended burst instead shows sustained,
alternating modulation within roughly 1 ms of carrier startup, continuing
through the usual idle cutoff. Compare these directly in `receive-startup.png`.
That plot uses a centered 32 µs average of CH2 minus CH1; its onset timing is
approximate and does not establish the MCU comparator's transition times.

The known-payload fit recovers three complete `0102030405` frames, all with
valid header, stop and row/column parity, starting at **+23.7238, +56.4918
and +89.2598 ms relative to carrier startup**.
The bit period is 512 µs. At that fitted phase, the next header starts
**23.7238 ms after carrier startup**, whereas the normal idle cutoff is only
7.16 ms after startup. Thus this observed extension occurs before the next
header of the recovered stream, supporting an activity-based extension rule.

**Implementation inference:** qualify a run of plausible receive transitions
during an idle probe, keep the carrier active while seeking a header and valid
frames, and return to idle polling on an acquisition timeout. A full header
should not be required to extend the initial probe. The exact interval tolerance,
qualification count and timeout must be chosen and tested against comparator
output; the analog record does not expose the factory software's internal rule.

The carrier-off interval before restart is **185.414 ms**. The restarted burst
continues through the record end, so its total duration is unknown. This is an
observed restart after the read; it does not establish whether tag removal,
a fixed delay or another condition governs all restarts.

Derived results are saved in `transition-analysis.json` beside the acquisition.
Its stored times use the raw scope axis. Convert to the carrier-relative axis
with `t_carrier = t_raw - (-0.1346800 s)`. `receive-startup.png` already uses
each burst's carrier startup as its local origin.

### Full-memory record and reproducible analysis

Use rigol-mcp `export_acquisition` to save the complete stopped acquisition.
The reference dataset has these properties:

| Property | Value |
| --- | --- |
| Instrument | Rigol DS1104Z, firmware 00.04.05.SP1 |
| Source | RAW acquisition memory, BYTE transfer |
| Channels | Four aligned channels |
| Samples | 3,000,000 per channel |
| Sample interval | 80 ns / 12.5 MS/s |
| Duration | Approximately 240 ms |
| Samples per carrier cycle | 100 |
| Local export | `captures/acquisition-41akvfzx/manifest.json` |

The manifest records actual preambles, conversion formulas, channel settings,
chunk counts, and completeness. It reports aligned timestamps and complete
transfers. Use waveform preambles to determine full-record duration and sample
rate; display timebase settings can describe only a zoomed portion of the record.

Voltage conversion is `(sample - yorigin - yreference) * yincrement` and time
conversion is `xorigin + (index - xreference) * xincrement`, with zero-based
sample indices. Probe scaling is already applied: do not multiply by ten again.

`get_waveform(raw_data=true)` exports about 1,200 **screen** points,
not full acquisition memory. A 33 ms screen export undersamples the carrier;
filtering cannot repair that aliasing. The existing short-window cleaner rejects
fewer than four samples per carrier period.

```powershell
.\.venv\Scripts\python.exe -m pip install -r requirements-analysis.txt
.\.venv\Scripts\python.exe tools/analyze_rfid_acquisition.py captures/acquisition-41akvfzx/manifest.json 0102030405
.\.venv\Scripts\python.exe tools/build_rfid_viewer.py captures/acquisition-41akvfzx
```

Outputs include `em4100-analysis.json`, `cleaned-difference.npz`,
`em4100-full-overlay.png`, and the standalone `em4100-viewer.html`. The viewer
offers frame/bit selection, measured half-bit levels, parity checks, zoom and
pan. Files under `captures/` are git-ignored local bench artifacts: archive or
share the export separately when transferring this experiment to another host.

`analyze_rfid_acquisition.py` currently assumes the mapping above, standard
RF/64 EM4100 and the original scope time axis, with a fixed fitting cutoff
at raw −1 ms. Its stored timestamps and generated viewer retain that raw axis;
convert them to the carrier reference for the measurements reported here. It fits phase, polarity, gain, and ±2% bit-period variation to the
supplied payload. It then recovers bits from measured first/second half-bit
means and checks framing/parity. **This is known-template-assisted analysis,
not a blind decoder.** `overlay_em4100.py` supports short-window overlays;
a complete frame is required to validate the payload and parity.

Carrier and buzzer edge-group measurements are saved locally in
`carrier-timing-analysis.json` alongside the acquisition. These use 2.5 V
threshold crossings on the raw samples, independently of the EM4100 fit.

For `acquisition-41akvfzx`, convert stored times using
`t_cutoff = t_raw - (-0.00016240 s)`. Startup is not present in this record,
so the frame times below explicitly use **carrier cutoff as t = 0**:

| Frame start relative to carrier cutoff | Recovered payload | Header / stop | Row / column parity |
| --- | --- | --- | --- |
| −110.81364 ms | `0102030405` | Pass | Pass |
| −78.04564 ms | `0102030405` | Pass | Pass |
| −45.27764 ms | `0102030405` | Pass | Pass |

Fitted bit period was **512 µs**, frame period **32.768 ms**. At the fitted
polarity, a data one has a more positive P3.6 − P3.7 first half than second
half. For the documented external-pair selection, CMPRES has the opposite polarity
to this voltage difference. Its measured waveform and filter delay remain to
be verified on the development unit.

## Appendix: existing diagnostic image

`rfid_firmware_test` is a separate bring-up image running at nominal 24 MHz
with UART1 at 9600 baud, 8N1. Its current commands provide carrier and comparator
checks; the streaming decoder described above remains to be implemented.
The default PlatformIO environment is `matrix_test`.

The diagnostic configures `CMPCR1=0x84` for the external input pair, with no
edge interrupts or physical output routing. `CMPCR2=0` retains the analog
filter with zero digital delay. These are bring-up values requiring measurement,
not validated receive-filter settings.

### Build and flash

```powershell
.\.venv\Scripts\pio.exe run -e rfid_firmware_test
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\Flash-STC.ps1 -Port COM14 -Environment rfid_firmware_test
```

Use the bridge's current COM port. Flashing replaces the installed
firmware. Keep Arduino USB powered and power-cycle the keypad when prompted.
The image is not flashed as part of building it.

### Diagnostic commands

```powershell
.\.venv\Scripts\python.exe tools/rfid_test.py COM14 "?"
.\.venv\Scripts\python.exe tools/rfid_test.py COM14 LISTEN
.\.venv\Scripts\python.exe tools/rfid_test.py COM14 FIELD
.\.venv\Scripts\python.exe tools/rfid_test.py COM14 CAPTURE
```

1. Check the identity reply says `K7612 RFID TEST v1`.
2. `LISTEN` captures a 100 ms baseline with carrier disabled.
3. `FIELD` runs the carrier for one second, then returns P3.5 low. Verify
   approximately 125 kHz at **MCU P3.5**, rather than assuming the antenna
   circuit works because the timer was configured.
4. Take `CAPTURE` once without a tag and again with a tag already held at the
   coil. It starts the carrier and records for 100 ms, including startup.
   Each response is saved separately under `captures/rfid-*.txt`.

The carrier is off at boot and after every operation. P3.5 is held low to
turn Q3 off; this does not guarantee zero current elsewhere in the analog circuit.
All other GPIO are high impedance except UART TX. P3.6/P3.7 pull-ups are
disabled. The diagnostic also attempts to disable their digital receivers via
the disputed P3IE address; see the comparator setup above. Comparator output
routing stays disabled,
so it cannot drive the buzzer on P3.4. Relay, keypad, LEDs and Wiegand are
not operated by this image. Measure the circuit's supply current and analog
response during bring-up.

The factory measurements above support the carrier/input roles, but do not
validate this image's analog operating point or comparator settings. Keep the
factory reference unit unmodified; test replacement firmware on the development
unit.

Commands are case-insensitive ASCII lines, with CR/LF endings, no extra spaces.
`?` also works immediately without a newline. `HELP` lists commands.
`STOP` turns the carrier off while idle; send **`!`** for immediate burst abort.
During a burst, other incoming bytes are discarded. Send one command at a time
and wait for its `OK` response. The capture tool handles this and allows 15 s
for a complete dump. Keep only one program connected to the serial port.

### Diagnostic capture format and limits

`CAP TICKS=... INITIAL=... COUNT=... DROPPED=... MAX_GAP=... ABORT=...`
is followed by `EDGE timestamp level` rows and `OK CAPTURE CARRIER=OFF`.
Numbers are hexadecimal except the single-bit levels and ABORT. Timestamps,
TICKS and MAX_GAP use **0.5 us timer ticks** (divide by two for microseconds).
Each timestamp is relative to capture start; its level is the new raw CMPRES
value. Subtract successive timestamps to obtain pulse widths.

Timer0 mode 0, 1T, reload 0xFFA0 gives 24 MHz / (2 * 96) = 125 kHz.
Timer1 provides UART baud timing. Timer2 runs at 24 MHz / 12 = 2 MHz,
reloading zero; software accumulates unsigned differences across its wraps.
Oscillator calibration error affects all three timings.

The comparator is **polled**, not interrupt-captured. Up to 192 observed edges
are saved in 960 bytes of XRAM; extra observed edges increment a saturating
DROPPED count. MAX_GAP reports the largest polling interval, not the timestamp
resolution. Pulses shorter than the polling interval can be missed entirely,
even when DROPPED is zero. This diagnoses tag-envelope activity; it is not a
125 kHz waveform recorder or a validated decoder. UART printing occurs only
after the carrier has been turned off. A full/noisy buffer may contain only
startup transitions. A flat capture does not prove that no tag is present.

### Diagnostic software checks

```powershell
.\.venv\Scripts\python.exe tools/test_rfid_firmware.py
```

The SDCC/8051 simulator checks GPIO/register setup, carrier start/stop state,
timer-difference rollover, edge storage, capacity limits, saturating overflow,
and capture reset. It does not emulate STC timer clock routing, the comparator,
or the board's analog circuit. Verify those on hardware before adding decoding.
