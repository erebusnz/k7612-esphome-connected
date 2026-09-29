/* Measured K7612 matrix. UART1 9600 8N1, nominal 24 MHz.
 * Rows P0.0..3 (pins 29..32); columns P1.5/4/3 (pins 6/5/4).
 * Rows: open drain, one low at a time, others released.
 * Columns: high-impedance inputs; explicit 4.1k pull-ups on matrix pins.
 * Single-key diagnostic: multiple detected keys suppress new key events
 * until all keys have been released and debounced.
 */
#include <8051.h>
#include "output_commands.h"
__sfr __at (0x8e) AUXR;
__sfr __at (0x93) P0M1;
__sfr __at (0x94) P0M0;
__sfr __at (0x91) P1M1;
__sfr __at (0x92) P1M0;
__sfr __at (0x95) P2M1;
__sfr __at (0x96) P2M0;
__sfr __at (0xb1) P3M1;
__sfr __at (0xb2) P3M0;
__sfr __at (0xa2) P_SW1;
__sfr __at (0xba) P_SW2;
#define P0PU (*(volatile __xdata unsigned char *)0xfe10)
#define P1PU (*(volatile __xdata unsigned char *)0xfe11)
#define P0IE (*(volatile __xdata unsigned char *)0xfe30)
#define P1IE (*(volatile __xdata unsigned char *)0xfe31)

static const __code char keys[] = "123456789*0#";
static const __code unsigned char row_latch[] = {0xfe, 0xfd, 0xfb, 0xf7};
static const __code unsigned char column_mask[] = {0x20, 0x10, 0x08};
static const __code char hex[] = "0123456789ABCDEF";
static unsigned int last_raw;
static unsigned char released_rows[4], released_cols[4];
static unsigned char selected_rows[4], selected_cols[4];
static unsigned char held = 0xff, candidate = 0xff, debounce_count, blocked;
static volatile __idata unsigned char rx_buffer[64];
static volatile unsigned char rx_head, rx_tail, rx_overflow, tx_busy;
static char command_line[24];
static unsigned char command_length, discard_line, diagnostics;
static unsigned char output_high[3], output_state[3] = {2, 2, 2};
static void configuration(void);

void serial_interrupt(void) __interrupt (4)
{
    unsigned char value, next;
    if (RI) {
        value = SBUF; RI = 0;
        next = (rx_head + 1) & 63;
        if (next == rx_tail) rx_overflow = 1;
        else { rx_buffer[rx_head] = value; rx_head = next; }
    }
    if (TI) { TI = 0; tx_busy = 0; }
}

static void putc_uart(unsigned char value)
{
    tx_busy = 1; SBUF = value;
    while (tx_busy) { }
}
static void puts_uart(const __code char *text)
{
    while (*text) putc_uart(*text++);
}
static void hex16(unsigned int value)
{
    putc_uart(hex[(value >> 12) & 15]);
    putc_uart(hex[(value >> 8) & 15]);
    putc_uart(hex[(value >> 4) & 15]);
    putc_uart(hex[value & 15]);
}
static void hex8(unsigned char value)
{
    putc_uart(hex[value >> 4]); putc_uart(hex[value & 15]);
}
static void tick_1ms(void)
{
    TR0 = 0; TF0 = 0; TH0 = 0xf8; TL0 = 0x30; TR0 = 1;
    while (!TF0) { }
    TR0 = 0;
}

static unsigned int scan(void)
{
    unsigned char row, column, levels;
    unsigned int result = 0, bit_mask = 1;
    for (row = 0; row < 4; ++row) {
        /* Release first; never drive two rows low during a transition. */
        P0 |= 0x0f;
        tick_1ms();
        released_rows[row] = P0 & 0x0f;
        released_cols[row] = P1 & 0x38;
        P0 &= row_latch[row];
        tick_1ms();
        selected_rows[row] = P0 & 0x0f;
        levels = P1;
        selected_cols[row] = levels & 0x38;
        P0 |= 0x0f;
        for (column = 0; column < 3; ++column) {
            if (!(levels & column_mask[column])) result |= bit_mask;
            bit_mask <<= 1;
        }
    }
    return result;
}

static void key_event(const __code char *kind, unsigned char index)
{
    puts_uart("KEY "); puts_uart(kind); putc_uart(' ');
    putc_uart(keys[index]);
    puts_uart(" R"); putc_uart('0' + index / 3);
    puts_uart(" C"); putc_uart('0' + index % 3);
    puts_uart("\r\n");
}

static void update_key(unsigned int raw)
{
    unsigned char next = 0xff, i;
    unsigned int bits;
    if (raw && (raw & (raw - 1))) {
        if (!blocked) puts_uart("BLOCK MULTI RELEASE_ALL\r\n");
        blocked = 1;
    }
    if (blocked) {
        /* Cancel a held key explicitly; do not invent a physical release. */
        if (held != 0xff) { key_event("CANCEL", held); held = 0xff; }
        if (raw) debounce_count = 0;
        else if (++debounce_count >= 5) {
            blocked = 0; debounce_count = 0; candidate = 0xff;
            puts_uart("READY\r\n");
        }
        return;
    }
    if (raw) {
        bits = raw;
        for (i = 0; i < 12; ++i) {
            if (bits & 1) { next = i; break; }
            bits >>= 1;
        }
    }
    if (next != candidate) { candidate = next; debounce_count = 1; }
    else if (debounce_count < 5) ++debounce_count;
    if (debounce_count >= 5 && held != candidate) {
        if (held != 0xff) key_event("UP", held);
        held = candidate;
        if (held != 0xff) key_event("DOWN", held);
    }
}

static void status(void)
{
    unsigned char row;
    puts_uart("STATE RAW="); hex16(last_raw);
    puts_uart(" HELD="); putc_uart(held == 0xff ? '-' : keys[held]);
    puts_uart(blocked ? " BLOCKED=1\r\n" : " BLOCKED=0\r\n");
    /* Snapshot values collected during scan; printing occurs with rows released. */
    for (row = 0; diagnostics && row < 4; ++row) {
        puts_uart("SCAN R"); putc_uart('0' + row);
        puts_uart(" RELEASE="); hex8(released_rows[row]); putc_uart('/'); hex8(released_cols[row]);
        puts_uart(" SELECT="); hex8(selected_rows[row]); putc_uart('/'); hex8(selected_cols[row]);
        puts_uart("\r\n");
    }
}

static void output_name(unsigned char index)
{
    if (index == 0) puts_uart("LED1");
    else if (index == 1) puts_uart("LED2");
    else puts_uart("BUZZER");
}
static void release_output(unsigned char index)
{
    if (index == 0) { P2M1 |= 0x40; P2M0 &= 0xbf; }
    else if (index == 1) { P2M1 |= 0x80; P2M0 &= 0x7f; }
    else { P3M1 |= 0x10; P3M0 &= 0xef; }
    output_state[index] = 2;
}
static void drive_output(unsigned char index, unsigned char on)
{
    unsigned char level = on ? output_high[index] : !output_high[index];
    /* Set latch while high impedance, then enable this candidate output only. */
    release_output(index);
    if (index == 0) {
        P2_6 = level; P2M0 |= 0x40; P2M1 &= 0xbf;
    } else if (index == 1) {
        P2_7 = level; P2M0 |= 0x80; P2M1 &= 0x7f;
    } else {
        P3_4 = level; P3M0 |= 0x10; P3M1 &= 0xef;
    }
    output_state[index] = on;
}
static void outputs_status(void)
{
    unsigned char index;
    for (index = 0; index < 3; ++index) {
        puts_uart("OUT "); output_name(index);
        puts_uart(" STATE=");
        if (output_state[index] == 2) puts_uart("Z");
        else putc_uart(output_state[index] ? 'Y' : 'N');
        puts_uart(" ACTIVE="); putc_uart(output_high[index] ? 'H' : 'L');
        puts_uart("\r\n");
    }
}
static void identity(void)
{
    puts_uart("K7612 MATRIX TEST v3 9600\r\n");
}
static void execute_command(void)
{
    unsigned char target = 0, value = 0, kind;
    kind = parse_command(command_line, &target, &value);
    switch (kind) {
    case CMD_OUTPUT:
        drive_output(target, value);
        puts_uart("OK "); output_name(target); putc_uart(' ');
        putc_uart(value ? 'Y' : 'N'); puts_uart(" LEVEL=");
        putc_uart((value ? output_high[target] : !output_high[target]) ? 'H' : 'L');
        puts_uart("\r\n"); break;
    case CMD_POLARITY:
        release_output(target); output_high[target] = value;
        puts_uart("OK POL "); output_name(target); putc_uart(' ');
        putc_uart(value ? 'H' : 'L'); puts_uart(" RELEASED\r\n"); break;
    case CMD_RELEASE:
        release_output(0); release_output(1); release_output(2);
        puts_uart("OK RELEASE\r\n"); break;
    case CMD_DIAG:
        diagnostics = value;
        puts_uart(value ? "OK DIAG Y\r\n" : "OK DIAG N\r\n"); break;
    case CMD_STATUS:
        identity(); configuration(); outputs_status(); status(); break;
    default:
        puts_uart("ERR COMMAND\r\n"); break;
    }
}
static void receive_commands(void)
{
    unsigned char value;
    while (rx_tail != rx_head || rx_overflow) {
        if (rx_overflow) {
            ES = 0; rx_tail = rx_head; rx_overflow = 0; ES = 1;
            command_length = 0; discard_line = 1;
            puts_uart("ERR RX OVERFLOW; SEND NEWLINE\r\n");
            continue;
        }
        value = rx_buffer[rx_tail]; rx_tail = (rx_tail + 1) & 63;
        if (value == '\r' || value == '\n') {
            if (discard_line) discard_line = 0;
            else if (command_length) {
                command_line[command_length] = 0; execute_command();
            }
            command_length = 0;
        } else if (!discard_line) {
            if (value == '?' && !command_length) {
                command_line[0] = '?'; command_line[1] = 0; execute_command();
            } else if (value < 32 && value != '\t') {
                discard_line = 1; command_length = 0; puts_uart("ERR CHARACTER\r\n");
            } else if (command_length == sizeof(command_line) - 1) {
                discard_line = 1; command_length = 0; puts_uart("ERR LINE TOO LONG\r\n");
            } else command_line[command_length++] = value;
        }
    }
}

static void configuration(void)
{
    unsigned char saved = P_SW2, pu0, pu1, ie0, ie1;
    P_SW2 |= 0x80;
    pu0 = P0PU; pu1 = P1PU; ie0 = P0IE; ie1 = P1IE;
    P_SW2 = saved;
    puts_uart("CFG P0M1="); hex8(P0M1); puts_uart(" P0M0="); hex8(P0M0);
    puts_uart(" P1M1="); hex8(P1M1); puts_uart(" P1M0="); hex8(P1M0);
    puts_uart(" P0PU="); hex8(pu0); puts_uart(" P1PU="); hex8(pu1);
    puts_uart(" P0IE="); hex8(ie0); puts_uart(" P1IE="); hex8(ie1);
    puts_uart("\r\n");
}

void main(void)
{
    unsigned int heartbeat = 0;
    unsigned char saved;
    EA = 0; TR0 = 0; TR1 = 0;
    release_output(0); release_output(1); release_output(2);
    /* Configure only measured matrix pins plus the existing UART TX. */
    P0M1 |= 0x0f; P0M0 &= 0xf0; /* Inputs while setting row latch. */
    P0 |= 0x0f;
    P0M0 |= 0x0f; /* Open drain: latch 1 releases the row. */
    P1M1 |= 0x38; P1M0 &= 0xc7; /* High-impedance column inputs. */
    P1 |= 0x38;
    /* Extended GPIO registers: enable EAXFR for MOVX, preserve routing bits. */
    saved = P_SW2; P_SW2 |= 0x80;
    P0PU |= 0x0f; P1PU |= 0x38;
    P0IE |= 0x0f; P1IE |= 0x38;
    P_SW2 = saved;
    P_SW1 &= 0x3f;
    P3M1 &= 0xfd; P3M0 |= 0x02;
    SCON = 0x50; AUXR = 0x40; TMOD = 0x01;
    TH1 = 0xfd; TL1 = 0x8f; TR1 = 1;
    RI = 0; TI = 0; ES = 1; EA = 1;
    identity();
    configuration();
    puts_uart("MAP ROWS=29,30,31,32 COLS=6,5,4 KEYS=123456789*0#\r\n");
    outputs_status();
    while (1) {
        last_raw = scan();
        update_key(last_raw);
        tick_1ms();
        if (++heartbeat >= 220) { heartbeat = 0; status(); }
        receive_commands();
    }
}
