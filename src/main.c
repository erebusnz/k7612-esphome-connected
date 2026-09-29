/* STC8C2K32S2 LQFP32 input monitor. 24 MHz, UART1 9600 8N1.
 * All bonded GPIO are high-impedance inputs except P3.1 UART TX.
 * P3.0 UART RX excluded from events. No pull-ups or scan outputs.
 * Frame port order: P0, P1, P2, P3, P5. Unbonded bits masked out.
 */
#include <8051.h>
__sfr __at (0x8e) AUXR;
__sfr __at (0x93) P0M1;
__sfr __at (0x94) P0M0;
__sfr __at (0x91) P1M1;
__sfr __at (0x92) P1M0;
__sfr __at (0x95) P2M1;
__sfr __at (0x96) P2M0;
__sfr __at (0xb1) P3M1;
__sfr __at (0xb2) P3M0;
__sfr __at (0xc8) P5;
__sfr __at (0xc9) P5M1;
__sfr __at (0xca) P5M0;
__sfr __at (0xa2) P_SW1;

static const __code char hex[] = "0123456789ABCDEF";
static unsigned char stable[5], raw[5], changed[5];
static unsigned char count[40];

static void putc_uart(unsigned char value)
{
    TI = 0;
    SBUF = value;
    while (!TI) { }
    TI = 0;
}

static void puts_uart(const __code char *text)
{
    while (*text) putc_uart(*text++);
}

static void hex_byte(unsigned char value)
{
    putc_uart(hex[value >> 4]);
    putc_uart(hex[value & 15]);
}

static void sample(void)
{
    raw[0] = P0 & 0x0f;
    raw[1] = P1;
    raw[2] = P2;
    raw[3] = P3 & 0xfc;
    raw[4] = P5 & 0x30;
}

/* Debounce each pin separately; noisy neighbours cannot hide a button.
 * Four changed samples, nominally 5 ms apart, accept an edge.
 * Blocking UART reporting increases sampling intervals during heavy activity.
 */
static unsigned char debounce(void)
{
    unsigned char port, bit_index, mask, index, any = 0;
    sample();
    for (port = 0; port < 5; ++port) {
        changed[port] = 0;
        mask = 1;
        for (bit_index = 0; bit_index < 8; ++bit_index) {
            index = port * 8 + bit_index;
            if ((raw[port] ^ stable[port]) & mask) {
                if (++count[index] >= 4) {
                    stable[port] ^= mask;
                    changed[port] |= mask;
                    count[index] = 0;
                    any = 1;
                }
            } else count[index] = 0;
            mask <<= 1;
        }
    }
    return any;
}

static void frame(const __code char *kind, unsigned char include_changes)
{
    unsigned char port;
    puts_uart(kind);
    for (port = 0; port < 5; ++port) {
        putc_uart(' ');
        hex_byte(stable[port]);
    }
    if (include_changes) {
        for (port = 0; port < 5; ++port) {
            putc_uart(' ');
            hex_byte(changed[port]);
        }
    }
    puts_uart("\r\n");
}

static void tick_1ms(void)
{
    TR0 = 0;
    TF0 = 0;
    TH0 = 0xf8;
    TL0 = 0x30;
    TR0 = 1;
    while (!TF0) { }
    TR0 = 0;
}

void main(void)
{
    unsigned char i, ticks = 0;
    unsigned int heartbeat = 0;
    EA = 0;
    TR0 = 0;
    TR1 = 0;
    /* M1=1/M0=0: high-impedance input. Set M1 first. */
    P0M1 |= 0x0f; P0M0 &= 0xf0;
    P1M1 = 0xff; P1M0 = 0;
    P2M1 = 0xff; P2M0 = 0;
    P3M1 = 0xff; P3M0 = 0;
    P5M1 |= 0x30; P5M0 &= 0xcf;
    P_SW1 &= 0x3f;
    P3M1 &= 0xfd; P3M0 |= 0x02; /* UART TX is the sole output. */
    SCON = 0x50;
    AUXR = 0x40;
    TMOD = 0x01;
    TH1 = 0xfd; TL1 = 0x8f;
    TR1 = 1;
    for (i = 0; i < 50; ++i) tick_1ms();
    sample();
    for (i = 0; i < 5; ++i) stable[i] = raw[i];
    puts_uart("K7612 PIN WATCH v1 HIZ 9600 P0 P1 P2 P3 P5\r\n");
    frame("BASE", 0);
    while (1) {
        tick_1ms();
        if (++ticks == 5) {
            ticks = 0;
            if (debounce()) frame("EDGE", 1);
        }
        if (++heartbeat == 2000) {
            heartbeat = 0;
            frame("LIVE", 0);
        }
        if (RI) {
            i = SBUF;
            RI = 0;
            if (i == '?') {
                puts_uart("K7612 PIN WATCH v1 HIZ 9600 P0 P1 P2 P3 P5\r\n");
                frame("STATE", 0);
            }
        }
    }
}
