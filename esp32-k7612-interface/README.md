# ESP32 K7612 interface

A two-layer interface PCB for connecting the K7612 to an externally powered
ESP32 DevKit running ESPHome. Open `esp32-k7612-interface.kicad_pro` in
**KiCad 10.0.7 or later**. The project includes the schematic, routed PCB,
local symbol and footprint libraries, and `bom.csv`.

The circuit follows the [ESPHome connection guide](../docs/esphome-connection.md).
Use the [ESPHome package](../examples/esphome-wiegand.yaml) for GPIO25/26 inputs
and GPIO27 indication control.

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
| J2 supply | 1 | +12V input from external supply |
| J2 | 2 | GND |
| J3 ESP32 | 1 | 3V3 **from** the ESP32 DevKit |
| J3 | 2 | ESP32 GND |
| J3 | 3 | GPIO25, Wiegand D0 input |
| J3 | 4 | GPIO26, Wiegand D1 input |
| J3 | 5 | GPIO27, OPEN control output |
| J4 contacts | 1 | NO, directly connected to J1 pin 6 |
| J4 | 2 | NC, directly connected to J1 pin 7 |
| J4 | 3 | COMM, directly connected to J1 pin 8 |

J1, J2 and J4 use Phoenix MKDS 1,5 terminal-block footprints with 5.08 mm pitch.
J3 is a 1x5, 2.54 mm pin header. Pin 1 has a square pad. Check the terminal
block's mechanical drawing before substituting another series.

The ESP32 needs its own power supply. J3's 3V3 pin powers only the interface
buffer; the PCB does not regulate +12V or supply the ESP32. Grounds are common.
Use a suitably protected external 12 V supply; the PCB contains no fuse,
reverse-polarity protection or surge suppression.

NO, NC and COMM are separate pass-through nets with no connection to logic,
GND or +12V. They are reserved for future **low-voltage** relay-contact use.
This board is not designed for mains switching. The main keypad firmware does
not operate the relay. A load and its switching current require a separate
assessment of the relay, terminal blocks and copper paths.

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
- 0.25 mm signal tracks and default clearance; 0.75 mm +12V and relay-contact tracks.
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
