/* First bring-up image: UART1 only, STC8C2K32S2 at 24 MHz.
 * No matrix/relay/LED/buzzer pin assignments are assumed.
 * UART1: P3.0 RX, P3.1 TX, 9600 8N1. Send '?' for an identity reply.
 * Timer1 mode 0 (STC 16-bit auto-reload), 1T: baud = FOSC / 4 / 625.
 * Reference: STC8C technical manual UART1/Timer1 example.
 */
#include <8051.h>

__sfr __at (0x8e) AUXR;
__sfr __at (0xb1) P3M1;
__sfr __at (0xb2) P3M0;
__sfr __at (0xa2) P_SW1;

static void uart_putc(unsigned char value)
{
    TI = 0;
    SBUF = value;
    while (!TI) { }
    TI = 0;
}

static void uart_puts(const __code char *text)
{
    while (*text) uart_putc(*text++);
}

static void poll_rx(void)
{
    if (RI) {
        unsigned char value = SBUF;
        RI = 0;
        if (value == '?') {
            uart_puts("K7612 UART TEST v1 STC8C2K32S2 24000000 9600 OK\r\n");
        }
    }
}

static void tick_1ms(void)
{
    TR0 = 0;
    TF0 = 0;
    /* Timer0 12T: 24 MHz / 12 = 2 MHz, 2000 ticks = 1 ms. */
    TH0 = 0xf8;
    TL0 = 0x30;
    TR0 = 1;
    while (!TF0) poll_rx();
    TR0 = 0;
}

void main(void)
{
    unsigned int elapsed;
    EA = 0;
    TR0 = 0;
    TR1 = 0;
    P_SW1 &= 0x3f; /* UART1 on P3.0/P3.1. */
    P3M1 &= 0xfd;
    P3M0 |= 0x02; /* Only TX becomes push-pull; other GPIO left at reset. */
    SCON = 0x50;
    AUXR = 0x40; /* Timer1 1T, UART1 uses Timer1; Timer0 12T. */
    TMOD = 0x01; /* Timer1 16-bit auto-reload; Timer0 16-bit manual reload. */
    TH1 = 0xfd;
    TL1 = 0x8f; /* 65536 - 625: exactly 9600 baud at 24 MHz. */
    TR1 = 1;
    uart_puts("K7612 UART TEST v1 BOOT\r\n");
    while (1) {
        for (elapsed = 0; elapsed < 1000; ++elapsed) tick_1ms();
        poll_rx();
        uart_puts("K7612 UART TEST v1 ALIVE\r\n");
    }
}
