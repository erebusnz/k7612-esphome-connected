# Using the K7612 with its original firmware

The K7612 supports lock and unlock control through **Tuya MCU UART at 9600 baud, 8N1**, without replacing its factory firmware.

## Interface

| Item | Value |
| --- | --- |
| MCU marking | STC8C2K32S2, LQFP32 |
| UART | 9600 baud, 8 data bits, no parity, 1 stop bit |
| Signal configuration | Normal polarity, no flow control |
| Product ID | `yxhuuu3voctpkjqi` |
| Firmware version | `1.0.0` |
| Product response | `{"p":"yxhuuu3voctpkjqi","v":"1.0.0","m":2}` |
| VCC | Approximately 5 V |

## Wiring

Use a logic-level UART connection with compatible signal voltages. Cross the transmit and receive lines:

| Serial adapter or controller | Keypad | Purpose |
| --- | --- | --- |
| TX | RX | Commands to the keypad |
| RX | TX | Replies and state reports |
| GND | GND | Shared reference |
| Power output | Disconnected | Keypad uses its own supply |

For passive listening, connect only receiver RX to keypad TX and share GND. The interface uses logic-level UART, not RS-232 voltage levels.

The numbered keypad connector pinout is unverified. Identify connections by function; check connector orientation and continuity with power removed before assigning pin numbers.

An ESP32 requires suitable voltage translation between its 3.3 V GPIO and the keypad's approximately 5 V signal domain. Connect only one active transmitter to keypad RX. Powering the complete keypad through its VCC header is unverified; regulator and relay supply requirements are unknown.

## Data points

| DP | Type | Meaning |
| --- | --- | --- |
| 1 | Boolean (`0x01`) | `true` unlocks; `false` locks |
| 9 | Four-byte integer (`0x02`) | Unknown; observed value is `0` |

DP1 controls the lock/relay state. Physical door-position sensing is unverified. Duplicate DP1 reports are possible after a command.

DP9's meaning, units, range and writable behavior are unknown. Its ID and type match a countdown in Tuya's generic switch schema, but other Tuya lock schemas assign a different countdown function to DP9. A product-specific schema is needed to establish its meaning.

Automatic relocking behavior is unverified. The confirmed DP1 behavior applies with the keypad's lock timer disabled; it does not establish timer behavior with other settings.

Individual key events, PIN values, incorrect-PIN events and card IDs are not established UART capabilities. RFID is reported as **125 kHz**, but frequency, compatible card formats and card-event reporting remain unverified.

## UART protocol

Frames contain:

```text
55 aa | version | command | payload length (2 bytes) | payload | checksum
```

Payload lengths are big-endian. The checksum is the sum of all preceding bytes modulo 256. Host commands use version `00`; MCU replies use version `03`.

### Commands

| Operation | Frame |
| --- | --- |
| Heartbeat | `55 aa 00 00 00 00 ff` |
| Product information | `55 aa 00 01 00 00 00` |
| Working mode | `55 aa 00 02 00 00 01` |
| Query all status | `55 aa 00 08 00 00 07` |
| Unlock: DP1=true | `55 aa 00 06 00 05 01 01 00 01 01 0e` |
| Lock: DP1=false | `55 aa 00 06 00 05 01 01 00 01 00 0d` |

Lock and unlock commands actuate the lock directly. Basic queries and DP1 control work without a cloud connection or a cloud-connected status message.

### Replies

| Reply | Frame |
| --- | --- |
| First heartbeat after restart | `55 aa 03 00 00 01 00 03` |
| Subsequent heartbeat | `55 aa 03 00 00 01 01 04` |
| Working mode, empty payload | `55 aa 03 02 00 00 04` |
| DP1=true | `55 aa 03 07 00 05 01 01 00 01 01 12` |
| DP1=false | `55 aa 03 07 00 05 01 01 00 01 00 11` |
| DP9=0 | `55 aa 03 07 00 08 09 02 00 04 00 00 00 00 20` |

## Sending and receiving

Configure the UART connected to the keypad for **9600 8N1**, normal polarity and no flow control. Use a serial terminal with binary/hex transmission, a USB-to-UART adapter with a serial library, or a microcontroller UART.

Transmit the frames above as **raw bytes**, not ASCII representations of hex values. For example, a heartbeat is the seven bytes `55 aa 00 00 00 00 ff`. Do not append a newline or carriage return.

Keep receiving after transmission to allow delayed replies. UART reads can contain partial frames or multiple frames; buffer incoming bytes and parse them using the header and payload length. A complete frame contains payload length plus seven bytes. Validate the checksum before using a reply.

Command `0x07` carries data-point reports. Each data point contains:

```text
DP ID (1 byte) | type (1 byte) | data length (2 bytes, big-endian) | data
```

Boolean data is one byte (`00` or `01`); value data uses a four-byte big-endian integer. Process duplicate state reports without treating them as additional lock actions. Query all status with command `0x08` when a state refresh is needed.

## References

- [Tuya basic MCU protocol](https://developer.tuya.com/en/docs/mcu-standard-protocol/MCUSDK-wifi-base?id=Kd2bxu84567gk).
- [Tuya serial protocol and DP delivery](https://developer.tuya.com/en/docs/iot/tuya-cloud-universal-serial-port-access-protocol?id=K9hhi0xxtn9cb).
- [Generic switch functions](https://developer.tuya.com/en/docs/iot/product-standard-function-introduction?id=K9tp15cfskrgo) and [Wi-Fi lock schema](https://developer.tuya.com/en/docs/iot/wifi-doorlock-develop-process?id=K9eta08zvx4j7). These illustrate possible DP9 meanings; neither is a confirmed schema for this keypad.
