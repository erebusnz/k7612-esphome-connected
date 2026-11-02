# ESP32 K7612 interface

A two-layer interface PCB for connecting the K7612 to either an ESP32 DevKit (J3) or an
Apollo ESPHome Starter Kit (J5). Populate both connectors and plug in **one
controller at a time**. They share the 3.3 V supply and the OPEN control signal. Open `esp32-k7612-interface.kicad_pro` in
**KiCad 10.0.7 or later**. The project includes the schematic, PCB layout,
local symbol and footprint libraries, and `bom.csv`.

The circuit follows the [ESPHome connection guide](../docs/esphome-connection.md).
Use the [ESPHome package](../examples/esphome-wiegand.yaml) for GPIO25/26 inputs
and GPIO27 indication control. For the Apollo ESP32-C6, use the
[dedicated ESPHome configuration](../examples/esphome-apollo-k7612.yaml).

## Connections

Pin numbers below belong to this PCB, not the keypad's original connector.
Wire by the signal labels printed on the PCB. The table describes the PCB revision.
In the revised schematic the DevKit header is J2, with D1/GPIO26 on pin 4
and D0/GPIO25 on pin 5.

| Connector | Pin | Signal / connection |
| --- | --- | --- |
| J1 K7612 | 1 | +12V keypad supply |
| J1 | 2 | GND |
| J1 | 3 | OPEN |
| J1 | 4 | D0, green wire |
| J1 | 5 | D1, white wire |
| J1 | 6 | NO relay contact |
| J1 | 7 | NC relay contact |
| J1 | 8 | COMM relay common |
| J2 supply | 1 | +12V input from external supply |
| J2 | 2 | GND |
| J3 ESP32 | 1 | Regulated +5V **to** the ESP32 DevKit 5V input |
| J3 | 2 | 3V3 **from** the ESP32 DevKit |
| J3 | 3 | ESP32 GND |
| J3 | 4 | GPIO25, Wiegand D0 input |
| J3 | 5 | GPIO26, Wiegand D1 input |
| J3 | 6 | GPIO27, OPEN control output |
| J4 contacts | 1 | NO, directly connected to J1 pin 6 |
| J4 | 2 | NC, directly connected to J1 pin 7 |
| J4 | 3 | COMM, directly connected to J1 pin 8 |

J1, J2 and J4 use Phoenix MKDS 1,5 terminal-block footprints with 5.08 mm pitch.
J3 is a 1x6, 2.54 mm pin header. Pin 1 has a square pad. Check the terminal
block's mechanical drawing before substituting another series.

U2 regulates the J2 +12V input down to +5V for the DevKit. Connect J3 pin 1
to the DevKit’s 5V input and pin 3 to GND. The DevKit supplies 3V3 back
to U1 through J3 pin 2.
Use a suitably protected external 12 V supply; the PCB contains no fuse,
reverse-polarity protection or surge suppression.

NO, NC and COMM are separate pass-through nets with no connection to logic,
GND or +12V. They are reserved for future **low-voltage** relay-contact use.
This board is not designed for mains switching. The main keypad firmware does
not operate the relay. A load and its switching current require a separate
assessment of the relay, terminal blocks and copper paths.

## Apollo ribbon connector (J5)

Populate J5 with **JUSHUO AFC01-S14FCA-00**, LCSC **C262663**. It accepts a
0.3 mm thick, 14-contact, 0.5 mm pitch ribbon with the contacts facing the
bottom contacts of the latch. With the component side up, the ribbon enters
from the board edge beside J5. The silkscreen triangle and numeral identify pad 1.

| J5 accessory pad | Apollo signal | Connection on this board |
| --- | --- | --- |
| 1 | GND | J3-3, common ground |
| 2 | GPIO18 | Open |
| 3 | GPIO14 | Open |
| 4 | GPIO7 | J3-6, OPEN control through R5 to Q1’s gate |
| 5 | GPIO6 | J3-4, buffered D0 from U1 pin 6 |
| 6 | GPIO3 | J3-5, buffered D1 from U1 pin 4 |
| 7 | UART RX (Apollo) | Open |
| 8 | Switched 3.3 V | Open |
| 9 | UART TX (Apollo) | Open |
| 10 | SDA / GPIO1 | Open |
| 11 | Apollo +5V system rail | Open |
| 12 | SCL / GPIO0 | Open |
| 13 | GPIO2 | Open |
| 14 | Always-on +3V3 | J3-2, controller supply to U1 |

Power the Apollo by USB-C and the keypad through J2. U2’s regulated +5 V is
available on **J3-1 only** among the controller connections. Keep it isolated
from J5-11, which can connect toward the Apollo battery. GPIO7 high pulls
keypad OPEN low for red. GPIO7 low releases OPEN for green.

**Before first power:** Disconnect USB, battery and J2 power. With the actual
ribbon fitted, continuity-check J5-1 to Apollo GND and J5-14 to Apollo +3V3.
The Rev 4 mainboard and accessory numbering reverse through the ribbon
(mainboard 14 → accessory 1). The mapping is schematic-derived and has not
been bench-tested.

The Apollo YAML is a standalone node configuration. Provide `wifi_ssid` and
`wifi_password` in ESPHome secrets. It reserves GPIO6, GPIO3 and GPIO7 on
both expansion ports. The stock GPIO6 one-wire probe and GPIO3 PIR assignments
are omitted so they cannot contend with the keypad.

Mechanical references: [C262663](https://www.lcsc.com/product-detail/C262663.html)
and [JUSHUO family drawing](https://robu-prod-media.s3.ap-south-1.amazonaws.com/uploads/2025/04/2411220100_JUSHUO-AFC01-S50FCA-00_C262676-1.pdf).
The 14-position row spans 6.50 mm. Each signal land is 0.30 × 1.30 mm;
the two unnumbered mounting lands are 2.00 × 1.60 mm. The pad-to-pad clearance
rule applies only to J5’s 0.20 mm contact gaps.

Pinout references: [Apollo Rev 4 mainboard, page 1](https://github.com/ApolloAutomation/ESPHome-Starter-Kit/blob/main/hardware/electronics/ESPHome_C6_Board/ESPHome_C6_Board.pdf),
[temperature module](https://github.com/ApolloAutomation/ESPHome-Starter-Kit/blob/main/hardware/electronics/Modules/Temp_And_Humidity_Module.pdf)
and [button module](https://github.com/ApolloAutomation/ESPHome-Starter-Kit/blob/main/hardware/electronics/Modules/Button_Module.pdf).

## Circuit

- U1: SN74LVC2G17DBVR, SOT-23-6 dual non-inverting Schmitt buffer, powered at
  3.3 V. Pinout: 1=1A, 2=GND, 3=2A, 4=2Y, 5=VCC, 6=1Y.
- R1/R2: 1 kilohm series resistors from D0/D1 to the buffer inputs.
- R3/R4: 100 kilohm pull-downs on the buffer inputs.
- C1: 100 nF X7R ceramic, 16 V or higher, close to U1.
- Q1: AO3400A, SOT-23. Pinout: 1=gate, 2=source/GND, 3=drain/OPEN.
- R5: 1 kilohm between GPIO27 and the gate; R6: 100 kilohms gate-to-source.

Use 0805 resistors, 1% tolerance, rated at least 0.125 W. OPEN is approximately
5 V when released and requires about 0.5 mA to pull LOW. GPIO27 HIGH turns
Q1 on for **red**; LOW or an unpowered ESP32 releases OPEN for **green**.
The gate pull-down holds Q1 off during reset.

## PCB

- 100 x 65 mm, two copper layers, 1.6 mm nominal thickness.
- Four 3.2 mm mounting holes, 4 mm from each edge.
- 0.25 mm signal tracks with 0.20 mm escapes at J5; 0.75 mm +12V and relay-contact tracks.
- 0.8 mm vias with 0.4 mm drills.

This is a prototype design. Review connector fit, cable protection and the
intended loads before fabrication. Keep the ESP32 header wiring short and
validate the complete installation before use.

## Design checks

From this folder, with `kicad-cli` on PATH:

```sh
kicad-cli sch erc esp32-k7612-interface.kicad_sch --exit-code-violations -o erc.rpt
kicad-cli pcb drc esp32-k7612-interface.kicad_pcb --schematic-parity --exit-code-violations -o drc.rpt
```

## Library attribution

`Interface.pretty` contains unmodified footprints from the KiCad 10 official
footprint libraries, copyright KiCad library contributors. They retain the
[KiCad library license](https://www.kicad.org/libraries/license/), CC-BY-SA 4.0
with the KiCad design exception. See `LICENSE-KICAD-LIBRARIES.md`. The local
symbols describe this board's connector functions and component pinouts.

Component references: [TI SN74LVC2G17](https://www.ti.com/lit/ds/symlink/sn74lvc2g17.pdf)
and [AOS AO3400A](https://www.aosmd.com/sites/default/files/res/datasheets/AO3400A.pdf).
