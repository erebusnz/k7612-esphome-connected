# Connecting the K7612 to an ESP32 running ESPHome

Connect a K7612 running the replacement `keypad_reader` firmware to a classic
ESP32-WROOM-32 DevKit using Wiegand for credentials and OPEN for LED control.
UART stays disconnected and debugging remains OFF. Authorization belongs to
the controller or Home Assistant. The circuit below is a reference design;
validate it with your board, cable and power supplies before deployment.

## Why Wiegand rather than UART?

The installation goal is to keep the keypad at the door and the ESP32 at the
controller end of the existing keypad cable. Wiegand uses the K7612's existing
D0/D1 connector signals for credentials, with OPEN carrying indication control
back to the keypad. This reuses the field wiring without extending the internal
UART programming/debug connection down the cable.

| Existing cable conductor | Function at the ESP32/controller end |
| --- | --- |
| +12V | Keypad 12 V supply, separate from ESP32 logic power |
| GND | Common signal reference and supply return |
| D0 (green) | Wiegand zero pulses through the input buffer to GPIO25 |
| D1 (white) | Wiegand one pulses through the input buffer to GPIO26 |
| OPEN | Indication control through the GPIO27-driven MOSFET to GND |

This requires five conductors when power shares the cable. Verify the existing
conductor assignments and continuity before connecting; do not identify OPEN
or power solely by wire color. Place the buffer and MOSFET at the ESP32 end,
with short local connections from the buffer outputs to its GPIOs.

The firmware sends both RFID and submitted PINs as Wiegand frames, and ESPHome
can receive them through its Wiegand component. Normal use needs no UART
commands or debug output. UART remains a local flashing/diagnostic interface:
its MCU-level TX/RX signals are not an RS-232 or differential RS-485 cable
interface. Extending them over the field cable would require suitable line
transceivers and a different integration, rather than simply wiring TX/RX
to ESP32 pins. Turning UART debugging on is not required for Wiegand operation.

Cable length depends on cable capacitance, pull-up strength, voltage drop,
grounding and interference. No maximum cable length is specified for this
interface. Check signal levels and pulse edges at the receiving end on the
installed cable. A logic buffer does not provide cable surge protection.

## Electrical boundary

D0/D1 idle at approximately 5 V and transmit active-LOW pulses of approximately
100 microseconds. OPEN has an approximately 5 V pull-up with an effective
resistance of about 10 kilohms, requiring roughly 0.5 mA to pull LOW. These
are typical values, not guaranteed limits across board revisions.

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

OPEN is an input at the keypad. At the ESP32 end, provide an **open-drain
current-sink output**: the transistor either connects OPEN to common GND or
releases it. It does not drive a HIGH voltage onto the cable; the keypad's
own pull-up supplies that level. With this firmware, sinking gives red and
releasing gives green. No UART command is involved in changing the indication.

Although the MOSFET has ample current-sinking capacity, **OPEN is not a
high-current load**: it requires only about 0.5 mA when
fully pulled LOW. The external transistor is needed to keep the keypad's
5 V released level off the ESP32 GPIO, not because OPEN needs a large current.
This output controls indication only; it is not a lock, relay-coil or keypad
power driver. Do not add a stronger pull-up to make it a high-current circuit.

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

### LED control

| ESPHome red-indication switch | GPIO27 / MOSFET | OPEN | LEDs |
| --- | --- | --- | --- |
| OFF (boot default) | LOW / off | Released HIGH | Green |
| ON | HIGH / on | Pulled LOW | Red |

For red = armed/closed and green = disarmed/open, turn this switch ON when
armed/closed and OFF when disarmed/open. Drive it from confirmed controller
state, not merely from receiving any credential. The firmware assigns colors,
not alarm semantics. Local PIN submission temporarily overrides the indication
for 150 ms; it does not signify controller authorization.

The default green indication after ESP32 reset or power loss is not proof that
the alarm is disarmed or a door is open. `restore_mode: ALWAYS_OFF` handles software initialization;
the physical gate pull-down handles the high-impedance interval. A network loss
without a reset can leave the last color unchanged. Decide explicitly how your
controller should represent stale or unknown state. This signal does not unlock
a door or control the keypad relay.

## ESPHome package

Use [examples/esphome-wiegand.yaml](../examples/esphome-wiegand.yaml) as a package
in an existing ESPHome node with Wi-Fi, native API (preferably encrypted) and
logger configured. GPIO25/26/27 are the example pins for the classic WROOM-32;
check availability on your board and do not copy them to a C3/S3 without review.
Validate the package with your installed ESPHome version before flashing.

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

The K7612 schedules a nominal **150 ms idle gap between frames**, measured
from the final pulse release to the next frame's first pulse. This leaves
50 ms of nominal margin over the receiver timeout. A stalled receiver loop
can still merge or lose frames; avoid blocking ESPHome automations and verify
consecutive submissions under the intended Wi-Fi/API workload. The firmware
queue holds two outstanding frames; sustained excess input can drop credentials.

## Installation checks

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
5. Toggle the red-indication switch: ON must give red; OFF must give green.
   Verify ESP32 reset/power-loss behavior and the temporary PIN feedback.
6. Submit consecutive credentials under Wi-Fi/API load and verify that each
   produces a separate callback.
