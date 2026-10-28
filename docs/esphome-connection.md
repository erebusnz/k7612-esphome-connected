# Connecting the K7612 to an ESP32 running ESPHome

This is a proposed interface for the replacement `keypad_reader` firmware,
using a classic ESP32-WROOM-32 DevKit. The reader itself has been bench-tested;
this interface and ESPHome example have not yet been built or bench-qualified.
UART stays disconnected and debugging remains OFF. The ESP32 receives D0/D1
and controls OPEN; authorization belongs to the controller or Home Assistant.

## Electrical boundary

D0/D1 were measured at approximately 5 V idle HIGH with LOW pulses near
100 microseconds. Their internal driver and pull-up circuit is not fully
characterized. OPEN is confirmed active LOW, but its idle voltage and pull-up
current have not been measured. Do not assume OPEN is a 3.3 V input.

Do not connect any of these keypad wires directly to an ESP32 GPIO.
[Espressif's ESP32-WROOM-32 electrical limits](https://documentation.espressif.com/esp32-wroom-32_datasheet_en.html)
specify an input upper limit of VDD + 0.3 V, not 5 V tolerance. Setting an ESP32
pin to open-drain does not make the pad tolerant of the keypad's pull-up voltage.

Use the keypad's normal supply and a separately regulated supply suitable for
the ESP32 board. Never connect the keypad's 12 V supply to ESP32 3V3 or a 5 V
board input. Join keypad GND, ESP32 GND and interface GND. Do not join the
keypad's 5 V logic rail to ESP32 3V3. This design is not galvanically isolated.
For long outdoor cable runs or different ground potentials, use a protected,
isolated Wiegand receiver; the simple local interface below is not surge protection.

## D0/D1: non-inverting, 5 V-tolerant input buffer

Use one **SN74LVC2G17 dual Schmitt-trigger buffer**, powered from the **same
3.3 V rail as the ESP32**. Its inputs accept up to 5.5 V, its outputs preserve
polarity, and its Ioff feature supports power-down without backfeeding through
the buffer. Do not substitute a generic HC buffer without checking its ratings.
See the [TI datasheet](https://www.ti.com/lit/ds/symlink/sn74lvc2g17.pdf).

Wire by signal name; check the pin numbers for the exact package/breakout:

| Connection | Wiring |
| --- | --- |
| Green D0 wire | 1 kilohm series resistor -> buffer 1A |
| White D1 wire | 1 kilohm series resistor -> buffer 2A |
| Buffer 1Y | ESP32 GPIO25 |
| Buffer 2Y | ESP32 GPIO26 |
| Buffer VCC | ESP32 3V3 |
| Buffer GND | Common GND |
| Decoupling | 100 nF ceramic from VCC to GND, close to the buffer |
| Input bias | 100 kilohms from each A input to GND to avoid floating inputs when the keypad is absent |

The series resistors limit incidental current; they do not clamp overvoltage.
Confirm each A input remains within 0 to 5.5 V, including overshoot. Check that
the weak input bias does not pull the keypad's idle level below the buffer's
HIGH threshold. Do not add pull-ups to 5 V on the buffer outputs. Use normal,
non-inverted ESPHome inputs; the buffer already produces 3.3 V idle-HIGH,
active-LOW signals. Keep it close to the ESP32.

A resistor divider can work for known voltage and source impedance, but loads
an unknown keypad pull-up and can still back-power an unpowered ESP32. Merely
adding a 3.3 V pull-up does not remove an existing 5 V pull-up. A common BSS138
I2C level-shifter module depends on suitable open-drain wiring and pull-ups;
that topology has not been established here. The specified buffer avoids these
assumptions for the measured 5 V lines.

## OPEN: external pull-down transistor

Use an **AO3400A N-channel MOSFET** as a low-side switch. Its manufacturer
specifies on-resistance at 2.5 V gate drive, so a 3.3 V GPIO can turn it on.
It is rated for 30 V drain-source; the voltage must stay below that rating with
margin. See [AOS's part specifications](https://www.aosmd.com/products/mosfets/low-voltage-mosfets-12v-30v/ao3400a).
Check the actual component's gate, drain and source pinout.

```text
ESP32 GPIO27 ---- 1 kilohm ---- Gate
                                |
                            100 kilohms
                                |
Common GND ------------------- Source

Keypad OPEN ------------------ Drain
```

GPIO27 HIGH turns the MOSFET on and pulls OPEN to GND. GPIO27 LOW or an
unpowered ESP32 leaves it off, releasing OPEN. The gate-to-source resistor
keeps it off while the GPIO is high-impedance during reset. Do not swap source
and drain: the body diode would defeat the intended release behavior. No
additional OPEN pull-up is specified; use the keypad's existing bias.

Measure OPEN's released voltage and pull-down current before connecting this
circuit. Confirm it is a low-current signal input, not a power output. This
measurement is still outstanding; do not treat the MOSFET's rating as proof
of the connector's electrical behavior.

| ESPHome green-indication switch | GPIO27 / MOSFET | OPEN | LEDs |
| --- | --- | --- | --- |
| OFF (boot default) | LOW / off | Released HIGH | Red |
| ON | HIGH / on | Pulled LOW | Green |

For red = armed/closed and green = disarmed/open, turn this switch OFF when
armed/closed and ON when disarmed/open. Drive it from confirmed controller
state, not merely from receiving any credential. The firmware assigns colors,
not alarm semantics. During a local PIN submission, both LEDs flash for
150 ms before returning to OPEN state.

The default red indication after reset or ESP32 power loss is not proof that
the alarm is armed. `restore_mode: ALWAYS_OFF` handles software initialization;
the physical gate pull-down handles the high-impedance interval. A network loss
without a reset can leave the last color unchanged. Decide explicitly how your
controller should represent stale or unknown state. This signal does not unlock
a door or control the keypad relay.

## ESPHome package

Use [examples/esphome-wiegand.yaml](../examples/esphome-wiegand.yaml) as a package
in an existing ESPHome node with Wi-Fi, native API (preferably encrypted) and
logger configured. GPIO25/26/27 are proposed pins for the classic WROOM-32;
check availability on your board and do not copy them to a C3/S3 without review.
The example is source-reviewed against ESPHome 2026.10.0, not compiled or tested
on hardware. Validate with your installed ESPHome version before flashing.

```yaml
# Adjust this relative path for your ESPHome configuration directory.
packages:
  k7612: !include examples/esphome-wiegand.yaml
```

The [Wiegand component](https://esphome.io/components/wiegand/) accepts 26/34-bit
frames. Both RFID and `PIN#` arrive via `on_tag`, not `on_key`; this firmware
already collects PIN digits, so an ESPHome key collector is unnecessary.
The callback payload is a decimal string: `0102030405` in 34-bit mode becomes
`33752069`, while `1234#` becomes `1234`. The example prefixes these with
`k7612-` for Home Assistant tag events. RFID and PIN values can still collide.

The [native API tag-scanned action](https://esphome.io/components/api/#homeassistanttag_scanned-action)
requires allowing the ESPHome device to make Home Assistant action calls.
Events require a working connection; the example does not queue offline scans,
authorize credentials, or control a lock. These events can expose PIN values
in Home Assistant, so handle them as credentials.

### Frame separation and receiver scheduling

The [ESPHome 2026.10.0 receiver](https://github.com/esphome/esphome/blob/2026.10.0/esphome/components/wiegand/wiegand.cpp)
waits for **100 ms without a new bit** before delivering a frame. It checks
26/34-bit parity before `on_tag`; `on_raw` runs before that check and includes
parity bits. The component has no configurable frame timeout in that version.

The updated K7612 scheduler enforces **at least 150 ms idle between frames**,
from the final pulse release to the next frame's first pulse. That is 150.000 ms
at the normal 16-microsecond cadence, or 150.016 ms at the diagnostic
32-microsecond cadence. This replaces the previous 50 ms gap, which could merge
two queued credentials in ESPHome.

An SDCC/s51 test observes real firmware pin writes for two queued frames and
feeds their timing into a separate 100 ms idle-time receiver model. It checks
both formats, polarities and sample cadences. This establishes nominal framing
compatibility, not an ESP32 bench result. A subsequent Flipper/Rigol bench
capture measured shortest output gaps of 149.990 and 150.052 ms, consistent with
the nominal setting; all six captured frames had valid 34-bit parity. See the
[reader bench record](keypad-reader.md) for burst-test limits and evidence.
The extra nominal 50 ms provides processing
margin; a receiver loop stalled long enough can still merge or lose frames.
Verify consecutive submissions under your actual Wi-Fi/API workload. The queue
still holds only two outstanding frames, so sustained excess input can drop
credentials; the larger gap reduces maximum throughput.

## Bring-up checks

1. Measure keypad D0, D1 and OPEN relative to keypad GND with the ESP32
   disconnected. Verify the buffer voltage limits and OPEN pull-down load.
2. Power the buffer from ESP32 3V3. Scope both outputs before attaching the
   GPIOs: idle near 3.3 V, LOW near GND, with the original pulse polarity and
   roughly 100-microsecond pulse widths preserved.
3. Test with keypad powered and ESP32 off. Confirm the ESP32 3V3 rail is not
   being back-powered and the MOSFET releases OPEN. Also check keypad-off
   behavior while the ESP32 remains powered.
4. Present `0102030405`; expect `k7612-33752069`. Remove and re-present it,
   then enter `1234#`; expect `k7612-1234`. A held tag should not repeat.
5. Toggle the green-indication switch: ON must give green; OFF must give red.
   Verify ESP32 reset/power-loss behavior and the temporary both-on PIN flash.
6. Test two queued credentials with the updated firmware under Wi-Fi/API
   load and verify two distinct callbacks. Keep this as an integration test,
   separate from the existing STC simulator and Flipper/Rigol tests.
