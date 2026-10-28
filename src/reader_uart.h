#ifndef READER_UART_H
#define READER_UART_H
/* Included once by reader_firmware.h after the hardware register definitions.
 * Foreground produces bytes; the UART ISR consumes them. One slot stays empty. */
/* Production boots silently; the RFID diagnostic image retains verbose boot. */
#ifdef K7612_KEYPAD
#define READER_DEBUG_DEFAULT 0
#else
#define READER_DEBUG_DEFAULT 1
#endif
static __xdata unsigned char reader_debug;
static volatile __xdata unsigned char reader_tx[256];
static volatile unsigned char reader_tx_head, reader_tx_tail, reader_tx_busy;
static volatile unsigned char reader_abort;
static __xdata unsigned char reader_uart_active,uart_timer2_ready;
static __xdata unsigned int reader_tx_dropped;

/* Foreground-only check for a complete message. UART ISR can only free space;
 * no other producer may write between this check and the message. This does
 * not advance head. Keep the byte cast: the 256-byte ring wraps modulo 256. */
static unsigned char uart_reserve(unsigned char bytes)
{
    if(!reader_debug) return 0;
    if((unsigned char)(reader_tx_tail-reader_tx_head-1)>=bytes) return 1;
    ++reader_tx_dropped;
    return 0;
}

/* ES is disabled only around the producer's queue publication / idle kick.
 * The higher-priority sampler remains enabled throughout. */
static void uart_putc(unsigned char value)
{
    if(!reader_debug) return;
    if(reader_uart_active) {
        ES=0;
        if((unsigned char)(reader_tx_head+1)==reader_tx_tail) ++reader_tx_dropped;
        else {
            reader_tx[reader_tx_head++]=value;
            if(!reader_tx_busy) {
                TI=0; SBUF=reader_tx[reader_tx_tail++]; reader_tx_busy=1;
            }
        }
        ES=1;
        return;
    }
    TI=0; SBUF=value;
    while(!TI);
    TI=0;
}
void uart1_isr(void) __interrupt(4) __using(2)
{
    if(RI) {
        if(SBUF=='!') reader_abort=1;
        RI=0; /* Other bytes are deliberately discarded during READ. */
    }
    if(TI) {
        TI=0;
        if(reader_tx_head!=reader_tx_tail) SBUF=reader_tx[reader_tx_tail++];
        else reader_tx_busy=0;
    }
}
static void uart_puts(const __code char *text)
{
    if(!reader_debug) return;
    while (*text) uart_putc(*text++);
}
#endif
