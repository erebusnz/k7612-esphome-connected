# ESP32 K7612 interface

A two-layer interface PCB for connecting the K7612 to either an ESP32 DevKit (J2) or an
Apollo ESPHome Starter Kit (J4). Populate both connectors and plug in **one
controller at a time**. They share the 3.3 V supply and the OPEN control signal. Open `esp32-k7612-interface.kicad_pro` in
**KiCad 10.0.7 or later**. The project includes the schematic, PCB layout,
local symbol and footprint libraries, and `bom.csv`.

The circuit follows the [ESPHome connection guide](../docs/esphome-connection.md).
Use the [ESPHome package](../examples/esphome-wiegand.yaml) for GPIO25/26 inputs
and GPIO27 indication control. For the Apollo ESP32-C6, use the
[dedicated ESPHome configuration](../examples/esphome-apollo-k7612.yaml).

## Connections

Pin numbers below belong to this PCB, not the keypad's original connector.
Wire by the signal labels printed on the PCB.

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
| J2 ESP32 | 1 | Regulated +5V **to** the ESP32 DevKit 5V input |
| J2 | 2 | 3V3 **from** the ESP32 DevKit |
| J2 | 3 | ESP32 GND |
| J2 | 4 | GPIO26, Wiegand D1 input |
| J2 | 5 | GPIO25, Wiegand D0 input |
| J2 | 6 | GPIO27, OPEN control output |
| J3 contacts | 1 | COMM, directly connected to J1 pin 8 |
| J3 | 2 | NC, directly connected to J1 pin 7 |
| J3 | 3 | NO, directly connected to J1 pin 6 |

J1 and J3 use Phoenix MKDS 1,5 terminal-block footprints with 5.08 mm pitch.
J2 is a 1x6, 2.54 mm pin header. Pin 1 has a square pad. Check the terminal
block's mechanical drawing before substituting another series.

Choose one power arrangement when assembling the board:

- **Adapter-powered controller:** Fit U2 (LM7805-TD), C2 (330 nF input) and
  C3 (100 nF output). The 12 V supply at J1-1 feeds U2. Its regulated +5 V
  powers the DevKit through J2-1 or the Apollo through J4-11. When powering
  the Apollo this way, disconnect USB-C and leave its battery disconnected.
- **USB-powered controller:** Omit U2, C2 and C3 together and power the
  controller through USB. J4-11 and J2-1 are still wired together on the PCB.
  Treat that rail as potentially live when a controller is attached.

In either build, populate C1 for the Wiegand buffer. The active controller
supplies U1 with 3.3 V via J2-2 or J4-14. J1-2 is common ground.
Use a suitably protected external 12 V supply; the PCB contains no fuse,
reverse-polarity protection or surge suppression.

NO, NC and COMM are separate pass-through nets with no connection to logic,
GND or +12V. They are reserved for future **low-voltage** relay-contact use.
This board is not designed for mains switching. The main keypad firmware does
not operate the relay. A load and its switching current require a separate
assessment of the relay, terminal blocks and copper paths.

## Apollo ribbon connector (J4)

Populate J4 with **JUSHUO AFC01-S14FCA-00**, LCSC **C262663**. It accepts a
0.3 mm thick, 14-contact, 0.5 mm pitch ribbon with the contacts facing the
bottom contacts of the latch. With the component side up, the ribbon enters
from the board edge beside J4. The silkscreen triangle and numeral identify pad 1.

| J4 accessory pad | Apollo signal | Connection on this board |
| --- | --- | --- |
| 1 | GND | J2-3, common ground |
| 2 | GPIO18 | Open |
| 3 | GPIO14 | Open |
| 4 | GPIO7 | J2-6, OPEN control through R5 to Q1’s gate |
| 5 | GPIO6 | J2-5, buffered D0 from U1 pin 6 |
| 6 | GPIO3 | J2-4, buffered D1 from U1 pin 4 |
| 7 | UART RX (Apollo) | Open |
| 8 | Switched 3.3 V | Open |
| 9 | UART TX (Apollo) | Open |
| 10 | SDA / GPIO1 | Open |
| 11 | Apollo +5V system rail | J2-1 and U2 output; supplies Apollo when U2/C2/C3 are fitted |
| 12 | SCL / GPIO0 | Open |
| 13 | GPIO2 | Open |
| 14 | Always-on +3V3 | J2-2, controller supply to U1 |

Pad 11 connects directly to the Apollo system rail. On Rev 4, Q6 can connect
that rail toward the battery outside the charge controller. GPIO7 high pulls
keypad OPEN low for red. GPIO7 low releases OPEN for green.

**Before first power:** Disconnect USB, battery and the J1 supply. With the actual
ribbon fitted, continuity-check J4-1 to Apollo GND, J4-11 to Apollo +5V and J4-14 to Apollo +3V3.
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
rule applies only to J4’s 0.20 mm contact gaps.

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
- 0.25 mm signal tracks with 0.20 mm escapes at J4; 0.75 mm +12V and relay-contact tracks.
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
