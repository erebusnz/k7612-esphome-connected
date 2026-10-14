/* K7612 RFID bring-up: nominal 24 MHz, UART1 9600 8N1.
 * P3.5/pin18 -> R17 -> R20 -> Q3 base: candidate carrier drive.
 * P3.6/pin19 -> R23 -> detector; P3.7/pin20 -> C19 -> P3.6.
 * Streaming EM4100 reader; analog operating limits remain unverified.
 * STC8C manual chapters 12/13/15: T0CLKO, Timer2, comparator.
 */
#include <8051.h>
#include <string.h>
#include "rfid_capture.h"
#include "rfid_decoder.h"
#include "rfid_presence.h"

__sfr __at (0x8e) AUXR;
__sfr __at (0x8f) INTCLKO;
__sfr __at (0x93) P0M1;
__sfr __at (0x94) P0M0;
__sfr __at (0x91) P1M1;
__sfr __at (0x92) P1M0;
__sfr __at (0x95) P2M1;
__sfr __at (0x96) P2M0;
__sfr __at (0xb1) P3M1;
__sfr __at (0xb2) P3M0;
__sfr __at (0xb3) P4M1;
__sfr __at (0xb4) P4M0;
__sfr __at (0xc9) P5M1;
__sfr __at (0xca) P5M0;
__sfr __at (0xa2) P_SW1;
__sfr __at (0xba) P_SW2;
__sfr __at (0xaf) IE2;
__sfr __at (0xb7) IPH;
__sfr __at (0xd1) T4T3M;
__sfr __at (0xd2) T4H;
__sfr __at (0xd3) T4L;
__sfr __at (0xd4) T3H;
__sfr __at (0xd5) T3L;
__sfr __at (0xd6) T2H;
__sfr __at (0xd7) T2L;
__sfr __at (0xe6) CMPCR1;
__sfr __at (0xe7) CMPCR2;
#define P3PU (*(volatile __xdata unsigned char *)0xfe13)

static const __code char hex[] = "0123456789ABCDEF";
static __xdata char command[16];
static unsigned char command_length, discard_line;
/* Foreground owns clock accumulation and decoder. ISR-owned state is volatile.
 * UART and sampler queues use byte indices: publish data before advancing head. */
static volatile unsigned int beep_edges_left;
static volatile unsigned char beep_active;
static unsigned char reader_clock_active;
static __xdata unsigned char reader_tx[256];
static volatile unsigned char reader_tx_head, reader_tx_tail, reader_tx_busy;
static volatile unsigned char reader_abort;
static unsigned char reader_uart_active,uart_timer2_ready;
static volatile unsigned char sampler_probe;
static volatile unsigned int sampler_window;
static volatile unsigned char sampler_window_done;
static unsigned char read_sample_ticks=32;
static unsigned int read_window;
static unsigned char read_trace;
/* Reuse the raw capture timestamps: 384 packed 16-bit edge records.
 * Bit zero holds the old level; timestamp precision is 1 us in TRACE only. */
#define TRACE_CAPACITY (RFID_EDGE_CAPACITY*2u)
#define trace_data ((__xdata unsigned int *)edge_time)
static __xdata unsigned int trace_count;
static __xdata unsigned int reader_tx_dropped;
static unsigned int reader_clock_previous;
static __xdata unsigned long reader_clock_ticks;
static void reader_clock_update(unsigned int now)
{
    reader_clock_ticks+=(unsigned int)(now-reader_clock_previous);
    reader_clock_previous=now;
}
static unsigned int timer_read(void);
static void beep_stop(void);

/* ES is disabled only around the producer's queue publication / idle kick.
 * The higher-priority sampler remains enabled throughout. */
static void uart_putc(unsigned char value)
{
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
    while (*text) uart_putc(*text++);
}
/* Boot uses the already bench-verified Timer1 baud path. Test the new
 * Timer2 route before repurposing Timer1, with bounded failure recovery. */
static unsigned char uart_use_timer2(void)
{
    unsigned int guard;
    if(uart_timer2_ready) return 1;
    uart_puts("UART TIMER2 PROBE\r\n");
    AUXR=0xc5; /* Timer2 stopped; 1T, timer mode, UART1 selects Timer2. */
    T2L=0x8f; T2H=0xfd; /* Use the manual's low-then-high write order. */
    AUXR |= 0x10;
    TI=0; SBUF='U';
    for(guard=0;guard<60000u && !TI;++guard);
    if(!TI) {
        AUXR=0xc0; /* Timer1 is still running at the known baud rate. */
        TI=0;
        uart_puts("\r\nERR TIMER2 UART TIMEOUT; RFID OFF\r\n");
        return 0;
    }
    TI=0; TR1=0; uart_timer2_ready=1;
    uart_puts("ART TIMER2 OK\r\n");
    return 1;
}
static void hex8(unsigned char value)
{
    uart_putc(hex[value >> 4]); uart_putc(hex[value & 15]);
}
static void hex16(unsigned int value)
{
    hex8(value >> 8); hex8(value);
}
static void hex32(unsigned long value)
{
    hex16(value >> 16); hex16(value);
}

static void carrier_off(void)
{
    TR0 = 0;
    P3_5 = 0;            /* Preload low before returning pin to GPIO. */
    INTCLKO &= 0xfe;
    TF0 = 0;
    P3M1 &= 0xdf; P3M0 |= 0x20; /* Q3 base held low, not floating. */
}
static void carrier_on(void)
{
    carrier_off();
    AUXR |= 0x80;        /* Timer0 1T. */
    TMOD &= 0xf0;        /* STC mode 0: 16-bit auto reload. */
    TH0 = 0xff; TL0 = 0xa0; /* 24 MHz / (2 * 96) = 125 kHz. */
    INTCLKO |= 0x01;     /* Only T0CLKO/P3.5, never T1CLKO/buzzer. */
    TR0 = 1;
}
static unsigned int timer_read(void)
{
    unsigned char high,low;
    do { high=T3H; low=T3L; } while(high!=T3H);
    return ((unsigned int)high<<8)|low;
}
static void timer_start(void)
{
    T4T3M &= 0xf0; /* Timer3 stopped, timer mode, 12T, no pin output. */
    T3H=0; T3L=0;
    T4T3M |= 0x08; /* 2 MHz free-running timestamps, reload zero. */
    reader_clock_previous=0;
}
static void beep_stop(void)
{
    IE2 &= ~0x40; T4T3M &= ~0x80;
    beep_active=0; beep_edges_left=0; P3_4=1;
}
static void beep_start(void)
{
    if(beep_active) return;
    IE2 &= ~0x40;
    T4T3M &= 0x0f; /* Timer4 12T, timer mode, pin output disabled. */
    T4H=0xfe; T4L=0x70; /* 65536-400: 200 us half-period. */
    beep_edges_left=1020; beep_active=1; P3_4=1;
    IE2 |= 0x40; T4T3M |= 0x80;
}
void timer4_isr(void) __interrupt(20) __using(2)
{
    /* Timer4 interrupt request is acknowledged by hardware on ISR entry. */
    P3_4=!P3_4;
    if(!--beep_edges_left) {
        T4T3M &= ~0x80; IE2 &= ~0x40;
        beep_active=0; P3_4=1;
    }
}
#ifdef K7612_KEYPAD
#include "keypad_service.h"
#endif
#include "rfid_sampler.h"
void timer1_isr(void) __interrupt(3) __using(1)
{
    unsigned char high,low,level;
    /* Do not call foreground timer_read(): SDCC locals are not reentrant. */
    do { high=T3H; low=T3L; } while(high!=T3H);
    level=CMPCR1&1;
    sampler_tick(((unsigned int)high<<8)|low,level);
    if(sampler_probe && !--sampler_probe) { ET1=0; TR1=0; }
    if(sampler_window && !--sampler_window) {
        ET1=0; TR1=0;
        TR0=0; P3_5=0; INTCLKO &= ~1;
        sampler_window_done=1;
    }
}
static void sampler_start(void)
{
    ET1=0; TR1=0; TF1=0;
    sampler_reset(timer_read(),CMPCR1&1);
    TMOD &= 0x0f; AUXR |= 0x40; /* Timer1 mode0, 1T, 16-bit autoreload. */
    if(read_sample_ticks==32) { TH1=0xfe; TL1=0x80; }
    else { TH1=0xfd; TL1=0; } /* 16 or 32 us, 24 MHz, 1T. */
    sample_gap_limit=read_sample_ticks+16;
    PT1=1; IPH |= 0x08; /* Highest priority; UART/Timer4 stay lowest. */
    ET1=1; TR1=1;
}
static unsigned char abort_requested(void)
{
    unsigned char value;
    if(reader_uart_active) return reader_abort;
    if(!RI) return 0;
    value=SBUF; RI=0;
    if(value!='\r' && value!='\n') discard_line=1;
    if(value=='!') { beep_stop(); return 1; }
    return 0;
}
static void identity(void)
{
#ifdef K7612_KEYPAD
    uart_puts("K7612 KEYPAD RFID v1.0 RFID=8.15 UART=9600\r\n");
#else
    uart_puts("K7612 RFID TEST v8.15 FOSC=24000000 UART=9600 CARRIER=OFF\r\n");
#endif
    uart_puts("P3.5=T0CLKO P3.6/P3.7=COMPARATOR PAIR UNVERIFIED\r\n");
    uart_puts("CFG CMPCR1="); hex8(CMPCR1);
    uart_puts(" CMPCR2="); hex8(CMPCR2);
    uart_puts(" P3M1="); hex8(P3M1);
    uart_puts(" P3M0="); hex8(P3M0); uart_puts("\r\n");
    uart_puts("OK STATUS\r\n");
}
static void field_test(void)
{
    unsigned int previous, now;
    unsigned long ticks = 0;
    unsigned char aborted = 0;
    uart_puts("BEGIN FIELD 125000HZ 1000MS ABORT=!\r\n");
    timer_start(); previous = timer_read(); carrier_on();
    while (ticks < 2000000UL) {
        now = timer_read(); ticks += (unsigned int)(now - previous); previous = now;
        if (abort_requested()) { aborted = 1; break; }
    }
    carrier_off(); T4T3M &= ~0x08;
    uart_puts(aborted ? "OK ABORT CARRIER=OFF\r\n" : "OK FIELD CARRIER=OFF\r\n");
}
static void capture(unsigned char drive)
{
    unsigned char i, aborted = 0;
    uart_puts(drive ? "BEGIN CAPTURE 125000HZ 100MS\r\n" : "BEGIN LISTEN CARRIER=OFF 100MS\r\n");
    timer_start();
    capture_reset(timer_read(), CMPCR1 & 1);
    if (drive) carrier_on();
    while (elapsed_ticks < 200000UL) {
        capture_sample(timer_read(), CMPCR1 & 1);
        if (abort_requested()) { aborted = 1; break; }
    }
    carrier_off(); T4T3M &= ~0x08;
    uart_puts("CAP TICKS="); hex32(elapsed_ticks);
    uart_puts(" INITIAL="); uart_putc('0' + initial_level);
    uart_puts(" COUNT="); hex8(edge_count);
    uart_puts(" DROPPED="); hex16(dropped);
    uart_puts(" MAX_GAP="); hex16(max_gap);
    uart_puts(" ABORT="); uart_putc('0' + aborted); uart_puts("\r\n");
    for (i = 0; i < edge_count; ++i) {
        uart_puts("EDGE "); hex32(edge_time[i]); uart_putc(' ');
        uart_putc('0' + edge_level[i]); uart_puts("\r\n");
    }
    uart_puts("OK CAPTURE CARRIER=OFF\r\n");
}
/* Queued sampling runs independently of foreground parity and formatting.
 * A capture fault postpones absence expiry: lost data is not proof of removal. */
static void read_tags(void)
{
    static __xdata unsigned int now,stamp,reads,peak;
    static __xdata unsigned int loops;
    static __xdata unsigned int late,over;
    static __xdata unsigned int prefixes;
    static __xdata unsigned long report_at;
    unsigned char j,r,c,nibble,notify,beep_pending=0,old_level,had_fault;
    if(!uart_use_timer2()) return;
    reads=0; report_at=0;
    presence_reset(); late=over=loops=0;
    prefixes=0;
    if(read_trace) trace_count=0;
    timer_start(); reader_clock_ticks=0; reader_clock_previous=timer_read();
    reader_clock_active=1;
    uart_puts("BEGIN READ RF64 SAMPLE_TICKS="); hex8(read_sample_ticks);
    uart_puts(" DECODER="); hex8(reader_clock_tracking);
    uart_puts(" FILTER_MIN=24US CONFIRM=1 ABSENT=1.5S STOP=!\r\n");
    reader_tx_head=reader_tx_tail=reader_tx_busy=reader_abort=0; reader_tx_dropped=0;
    /* Bounded startup probe returns control even if the ISR monopolizes CPU. */
    RI=TI=0; ES=0; sampler_window=0; sampler_probe=128;
    now=timer_read(); sampler_start(); EA=1;
    for(stamp=0;sampler_probe && stamp<60000u;++stamp);
    ET1=0; TR1=0; EA=0;
    stamp=timer_read()-now;
    uart_puts("SAMPLE PROBE ELAPSED="); hex16(stamp);
    uart_puts(" MAX="); hex16(sample_max_gap);
    uart_puts(" FAULT="); hex8(sample_fault);
    uart_puts(" LEFT="); hex8(sampler_probe); uart_puts("\r\n");
    if(sample_fault || sampler_probe) {
        reader_clock_active=0; T4T3M &= ~0x08;
        uart_puts("ERR SAMPLE PROBE; RFID OFF\r\n"); return;
    }
    /* Check buffered TX separately, before enabling continuous sampling. */
    reader_uart_active=1; RI=TI=0; ES=1; EA=1;
    uart_puts("UART IRQ OK\r\n");
    for(stamp=0;reader_tx_busy && stamp<60000u;++stamp);
    if(reader_tx_busy) {
        ES=0; EA=0; reader_uart_active=reader_clock_active=0;
        T4T3M &= ~0x08;
        uart_puts("\r\nERR UART IRQ; RFID OFF\r\n"); return;
    }
    now=timer_read(); reader_reset(now,CMPCR1&1);
#ifdef K7612_KEYPAD
    keypad_reset(now);
#endif
    reader_clock_previous=now; reader_clock_ticks=0;
    sampler_window=read_window; sampler_window_done=0;
    carrier_on(); sampler_start();
    while(!abort_requested() && !sampler_window_done) {
        ++loops;
        now=timer_read(); reader_clock_update(now);
#ifdef K7612_KEYPAD
        keypad_tick(now);
#endif
        /* Only a few stores with sampling masked; decoder reset stays outside. */
        had_fault=sample_fault;
        if(had_fault) {
            ET1=0; had_fault=sample_fault;
            sample_tail=sample_head;
            sample_candidate=sample_level=CMPCR1&1;
            sample_fault=0; if(!sampler_window_done) ET1=1;
        }
        if(had_fault) {
            if(had_fault&1) ++late;
            if(had_fault&2) ++over;
            reader_reset(now,CMPCR1&1);
            if(presence_count) presence_last_seen=reader_clock_ticks;
        }
        if(sample_tail!=sample_head) {
            stamp=sample_time[sample_tail]; old_level=sample_old[sample_tail];
            sample_tail=(sample_tail+1)&127;
            if(read_trace && reader_clock_ticks>=400000UL) {
                trace_data[trace_count++]=(stamp&0xfffeu)|old_level;
                if(trace_count==TRACE_CAPACITY) break;
            }
            decoder_transition(stamp,old_level);
            if(reader_prefix_ready) {
                if(presence_observed_prefix(reader_prefix,reader_clock_ticks)) ++prefixes;
                reader_prefix_ready=0;
            }
            if(reader_valid) {
                ++reads;
                notify=presence_acquired(reader_frames[reader_frame_count-1],reader_clock_ticks);
                if(notify) {
#ifdef K7612_KEYPAD
                    keypad_beep_request();
#else
                    beep_start(); beep_pending=1;
#endif
                    /* Reserve enough space for an entire frame message. */
                    if((unsigned char)(reader_tx_tail-reader_tx_head-1)>=64) {
                        uart_puts("Frame hex: ");
                        for(j=0;j<8;++j) hex8(reader_frames[reader_frame_count-1][j]);
                        uart_puts("\r\nData: ");
                        for(r=0;r<10;++r) {
                            nibble=0;
                            for(c=0;c<4;++c) nibble=(nibble<<1)|frame_bit(reader_frames[reader_frame_count-1],9+r*5+c);
                            uart_putc(hex[nibble]);
                        }
                        uart_puts("\r\n");
                    } else ++reader_tx_dropped;
                }
                reader_frame_count=reader_valid=reader_full=0;
            } else if(reader_full) reader_frame_count=reader_full=0;
        } else {
            /* Queue drained: silence cannot wrap around into a false interval. */
            if(edge_seen && (unsigned int)(now-decoder_edge)>2000) {
                edge_seen=0; decoder_sync_reset(); header_reset(); ++reader_resets;
            }
            if(!sample_fault && presence_expire(reader_clock_ticks)) {
                if((unsigned char)(reader_tx_tail-reader_tx_head-1)>=32)
                    uart_puts("TAG ABSENT REARMED\r\n");
                else ++reader_tx_dropped;
            }
        }
#ifndef K7612_KEYPAD
        if(beep_pending && !beep_active) {
            if((unsigned char)(reader_tx_tail-reader_tx_head-1)>=16)
                uart_puts("BEEP DONE\r\n");
            else ++reader_tx_dropped;
            beep_pending=0;
        }
#endif
        if((unsigned long)(reader_clock_ticks-report_at)>=2000000UL) {
            report_at=reader_clock_ticks;
            ET1=0; peak=sample_max_gap; if(!sampler_window_done) ET1=1;
            if((unsigned char)(reader_tx_tail-reader_tx_head-1)>=128) {
                uart_puts("READ TIME="); hex32(report_at);
                uart_puts(" VALID="); hex16(reads);
                uart_puts(" PREFIX="); hex16(prefixes);
                uart_puts(" RESETS="); hex16(reader_resets);
                uart_puts(" SAMPLE_MAX="); hex16(peak);
                uart_puts(" LATE="); hex16(late);
                uart_puts(" OVER="); hex16(over);
                uart_puts(" TX_DROP="); hex16(reader_tx_dropped);
                uart_puts(" CARRIER=ON\r\n");
            } else ++reader_tx_dropped;
            reads=reader_resets=prefixes=0;
        }
    }
    ET1=0; TR1=0; beep_stop(); carrier_off();
#ifdef K7612_KEYPAD
    keypad_reset(timer_read());
#endif
    reader_clock_update(timer_read());
    /* Hardware is stopped before the bounded UART tail is drained. */
    while(reader_tx_busy);
    ES=0; EA=0;
    reader_uart_active=reader_clock_active=0; T4T3M &= ~0x08;
    uart_puts("WINDOW DONE="); hex8(sampler_window_done);
    uart_puts(" TIME="); hex32(reader_clock_ticks);
    uart_puts(" LOOPS="); hex16(loops);
    uart_puts(" LEFT="); hex16(sampler_window);
    uart_puts(" VALID="); hex16(reads);
    uart_puts(" SAMPLE_MAX="); hex16(sample_max_gap);
    uart_puts(" LATE="); hex16(late); uart_puts(" OVER="); hex16(over);
    uart_puts("\r\nOK STOP\r\n");
    if(read_trace) {
        uart_puts("TRACE TICKS16 OLD_LEVEL PRECISION=1US SETTLE=200MS COUNT="); hex16(trace_count); uart_puts("\r\n");
        for(stamp=0;stamp<trace_count;++stamp) {
            uart_puts("EDGE "); hex16(trace_data[stamp]&0xfffeu); uart_putc(' ');
            uart_putc('0'+(trace_data[stamp]&1)); uart_puts("\r\n");
        }
        uart_puts("OK TRACE\r\n"); read_trace=0;
    }
}

static void execute(void)
{
    if (!strcmp(command, "?")) identity();
    else if (!strcmp(command, "READ")) { reader_clock_tracking=2; read_sample_ticks=32; read_window=0; read_tags(); }
    else if (!strcmp(command, "READPLL")) { reader_clock_tracking=1; read_sample_ticks=32; read_window=0; read_tags(); }
    else if (!strcmp(command, "READWIDTH")) { reader_clock_tracking=0; read_sample_ticks=32; read_window=0; read_tags(); }
    else if (!strcmp(command, "READ16")) { read_sample_ticks=32; read_window=50000u; read_tags(); }
    else if (!strcmp(command, "READ32")) { read_sample_ticks=64; read_window=50000u; read_tags(); }
    else if (!strcmp(command, "TRACE")) { read_trace=1; read_sample_ticks=32; read_window=50000u; read_tags(); }
    else if (!strcmp(command, "FIELD")) field_test();
    else if (!strcmp(command, "CAPTURE")) capture(1);
    else if (!strcmp(command, "LISTEN")) capture(0);
    else if (!strcmp(command, "STOP")) { carrier_off(); uart_puts("OK STOP\r\n"); }
    else if (!strcmp(command, "HELP")) {
        uart_puts("? READ READPLL READWIDTH READ16 READ32 TRACE FIELD CAPTURE LISTEN STOP HELP; ! stops reading\r\nOK HELP\r\n");
    } else uart_puts("ERR COMMAND\r\n");
}
static void receive(void)
{
    unsigned char value;
    if (!RI) return;
    value = SBUF; RI = 0;
    if (value == '!') {
        carrier_off(); command_length = 0; discard_line = 0;
        uart_puts("OK STOP\r\n");
    } else if (value == '\r' || value == '\n') {
        if (!discard_line && command_length) {
            command[command_length] = 0; execute();
        }
        command_length = 0; discard_line = 0;
    } else if (!discard_line) {
        if (value == '?' && !command_length) { identity(); return; }
        if (value < 32 || value > 126 || command_length == sizeof(command)-1) {
            discard_line = 1; command_length = 0;
            uart_puts("ERR LINE\r\n");
        } else {
            if (value >= 'a' && value <= 'z') value -= 'a' - 'A';
            command[command_length++] = value;
        }
    }
}
static void initialize_hardware(void)
{
    EA = 0; TR0 = 0; TR1 = 0; INTCLKO = 0; CMPCR1 = 0;
    /* Release all GPIO before enabling only UART TX and Q3 base drive.
     * No matrix scan, relay, Wiegand, LED, or comparator output pin.
     */
    P0M1 = P1M1 = P2M1 = P3M1 = P4M1 = P5M1 = 0xff;
    P0M0 = P1M0 = P2M0 = P3M0 = P4M0 = P5M0 = 0;
    P_SW2 |= 0x80;
    P3PU &= 0x1f;       /* No bias imposed on the unknown analog network. */
    /* P3IE at 0xfe33 is unverified on STC8C; leave it untouched. */
    P_SW2 &= 0x7f;
    carrier_off();
    beep_stop(); /* Preload high before enabling buzzer output. */
    P3M1 &= 0xef; P3M0 |= 0x10;
    P_SW1 &= 0x3f;
    P3M1 &= 0xfd; P3M0 |= 0x02;
    SCON=0x50; AUXR=0xc0; TMOD=0; IE=0; IE2=0; IP=0; IPH=0;
    /* Known working boot UART. read_tags() probes Timer2 before sampling. */
    TH1=0xfd; TL1=0x8f; TR1=1; uart_timer2_ready=0;
    T4T3M=0;
    /* External comparator pair; interrupts and physical output disabled.
     * Record raw CMPRES polarity rather than assuming analog pin roles.
     */
    CMPCR2 = 0; CMPCR1 = 0x84;
    RI = 0; TI = 0;
#ifdef K7612_KEYPAD
    keypad_hardware_init();
#endif
}
void main(void)
{
    initialize_hardware();
    identity();
    read_tags(); /* Standalone operation starts on every power-on/reset. */
    command_length=discard_line=0;
    while (1) receive();
}
