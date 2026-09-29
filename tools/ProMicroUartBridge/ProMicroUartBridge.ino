/*
 * ATmega32U4 Pro Micro: USB CDC (Serial) <-> hardware UART (Serial1).
 * Select the board's actual voltage/clock variant before uploading.
 *
 * For a VERIFIED 5 V board (see root README.md):
 *   Pro Micro TXO / D1 -> keypad RX (through the documented diode arrangement)
 *   Pro Micro RXI / D0 <- keypad TX
 *   Pro Micro GND     -- corrected keypad ground connection
 * Numbered UART header mapping is unverified; use signal function, not old probe labels.
 *   Leave VCC/RAW disconnected; keypad uses its own normal supply.
 * A 3.3 V Pro Micro requires suitable level translation.
 *
 * Host settings: 300..115200 baud, 5..8 data bits, none/even/odd parity,
 * 1 or 2 stop bits. Unsupported settings suspend forwarding.
 * DTR/RTS are not wired to target reset; cycle target power manually.
 * No text, echo, newline conversion, or diagnostic bytes are inserted.
 * BREAK and hardware flow control are not implemented.
 * The Arduino core's 1200-baud USB bootloader reset remains active.
 */

#include <Arduino.h>
#include <util/atomic.h>

#if !defined(__AVR_ATmega32U4__) || !defined(USBCON)
#error "Select an ATmega32U4 Pro Micro board with native USB."
#endif

static bool uartRunning = false;
static uint32_t activeBaud = 0;
static uint8_t activeConfig = SERIAL_8N1;

static void stopUart() {
  if (uartRunning) {
    Serial1.end(); // Finish queued TX and discard old RX before stopping.
    uartRunning = false;
  }
}

static bool updateUart() {
  uint32_t baud;
  uint8_t bits, parity, stops;
  bool hostOpen;
  // USB control requests update these values from an interrupt.
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    baud = Serial.baud();
    bits = Serial.numbits();
    parity = Serial.paritytype();
    stops = Serial.stopbits();
    hostOpen = Serial.dtr() || Serial.rts();
  }

  if (!hostOpen || baud < 300 || baud > 115200 || bits < 5 || bits > 8 ||
      parity > Serial_::EVEN_PARITY ||
      (stops != Serial_::ONE_STOP_BIT && stops != Serial_::TWO_STOP_BITS)) {
    stopUart();
    return false;
  }

  // AVR HardwareSerial's config is the USART character-format register.
  uint8_t config = (bits - 5) << 1;
  if (parity == Serial_::EVEN_PARITY) config |= 0x20;
  if (parity == Serial_::ODD_PARITY) config |= 0x30;
  if (stops == Serial_::TWO_STOP_BITS) config |= 0x08;

  if (!uartRunning || baud != activeBaud || config != activeConfig) {
    stopUart();
    Serial1.begin(baud, config);
    activeBaud = baud;
    activeConfig = config;
    uartRunning = true;
  }
  return true;
}

void setup() {
  Serial.begin(115200); // USB ignores this rate; the host supplies line coding.
}

void loop() {
  if (!updateUart()) {
    // Do not replay bytes sent while the UART configuration was unsupported.
    for (uint8_t i = 0; i < 32 && Serial.available() > 0; ++i) Serial.read();
    return;
  }

  // Bound each direction so sustained traffic cannot starve the other one.
  for (uint8_t i = 0; i < 32; ++i) {
    if (Serial.available() <= 0 || Serial1.availableForWrite() <= 0) break;
    const int value = Serial.read();
    if (value >= 0) Serial1.write(static_cast<uint8_t>(value));
  }
  for (uint8_t i = 0; i < 32; ++i) {
    if (Serial1.available() <= 0 || Serial.availableForWrite() <= 0) break;
    const int value = Serial1.peek();
    if (value < 0 || Serial.write(static_cast<uint8_t>(value)) != 1) break;
    Serial1.read();
  }
}
