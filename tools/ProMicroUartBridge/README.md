# Arduino Micro UART bridge

Upload [ProMicroUartBridge.ino](ProMicroUartBridge.ino) using the actual board profile. The tested Arduino Micro uses `arduino:avr:micro`; a Pro Micro must use its own voltage/clock variant. This upload programs the Arduino, not the keypad MCU.

The bridge forwards USB serial to hardware UART: D1/TX to keypad RX, D0/RX from keypad TX, and common ground. Leave VCC/RAW disconnected and use the keypad's own supply. See the [project README](../../README.md#connect-the-programmer) for signal voltages, corrected wiring, and TX backfeeding prevention. Historical Bus Pirate probe labels do not identify the current wires.

Host baud/parity settings are forwarded to the UART. Supported: 300–115200 baud, 5–8 data bits, no/even/odd parity, and one/two stop bits. DTR or RTS must be asserted (pyserial does this by default). Unsupported settings suspend forwarding. DTR/RTS do not control keypad reset; cycle keypad power manually. The Arduino core's 1200-baud USB reset behavior remains active.

To check the bridge, disconnect the keypad, join D1/TX directly to D0/RX, and run:

```powershell
.\.venv\Scripts\python.exe tools/test_uart_loopback.py COM14
```

Run from the repository root, using the current port. Remove the jumper before reconnecting the keypad. The tested bridge passed all 256 byte values at 2400/19200/115200 8E1 and 115200 8N1, and successfully programmed the STC using the [root flashing instructions](../../README.md#set-up-and-flash).
