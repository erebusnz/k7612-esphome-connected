#ifndef WIEGAND_SERVICE_H
#define WIEGAND_SERVICE_H
/* Foreground produces complete frames; Timer1 consumes them, MSB first.
 * Three ring slots hold at most two outstanding frames, including the active one. */
__sfr __at (0xc8) P5;
__sbit __at (0xcc) WG_D0;
__sbit __at (0xcd) WG_D1;
#ifndef K7612_WIEGAND_BITS
#define K7612_WIEGAND_BITS 34
#endif
#if K7612_WIEGAND_BITS != 26 && K7612_WIEGAND_BITS != 34
#error Wiegand length must be 26 or 34
#endif
/* -1 keeps outputs high-Z until WGPROBE succeeds. 0/1 may be set after
 * bench verification: MCU level that pulls the external wire LOW. */
#ifndef K7612_WIEGAND_ACTIVE
#define K7612_WIEGAND_ACTIVE -1
#endif
#if K7612_WIEGAND_ACTIVE < -1 || K7612_WIEGAND_ACTIVE > 1
#error Invalid Wiegand polarity
#endif
static __xdata unsigned char wg_bits=K7612_WIEGAND_BITS;
static __xdata unsigned char wg_enabled,wg_active;
static volatile __xdata unsigned char wg_frames[3][5];
static volatile __xdata unsigned char wg_head,wg_tail;
static __xdata unsigned char wg_pos,wg_mask,wg_byte,wg_low;
static __xdata unsigned int wg_wait;
static __xdata unsigned int wg_dropped;
static __xdata unsigned long pin_value,pin_last;
static __xdata unsigned char pin_digits,pin_invalid;

static void pin_clear(void) { pin_value=0; pin_digits=pin_invalid=0; }
static void wg_reset(void)
{
    wg_head=wg_tail=wg_pos=wg_low=0; wg_wait=0;
    if(wg_enabled) { WG_D0=!wg_active; WG_D1=!wg_active; }
    else { P5M1|=0x30; P5M0&=0xcf; }
    pin_clear();
}
static void wg_enable(unsigned char active)
{
    wg_active=active; wg_enabled=1;
    WG_D0=!active; WG_D1=!active;
    P5M0|=0x30; P5M1&=0xcf;
    wg_reset();
}
static unsigned char wg_parity(unsigned long value)
{
    static __xdata unsigned char p;
    p=0;
    while(value) { p^=(unsigned char)value&1; value>>=1; }
    return p;
}
static unsigned char wg_enqueue(unsigned long __xdata value)
{
    static __xdata unsigned char next,i,bit,half,ep,op;
    next=(wg_head+1)%3; half=(wg_bits-2)/2;
    if(wg_bits==26) value&=0xffffffUL;
    ep=wg_parity(value>>half);
    op=1^wg_parity(value&((1UL<<half)-1));
    if(!wg_enabled || next==wg_tail) { ++wg_dropped; return 0; }
    for(i=0;i<5;++i) wg_frames[wg_head][i]=0;
    for(i=0;i<wg_bits;++i) {
        if(!i) bit=ep;
        else if(i==wg_bits-1) bit=op;
        else bit=(unsigned char)(value>>(wg_bits-2-i))&1;
        if(bit) wg_frames[wg_head][i>>3]|=0x80>>(i&7);
    }
    wg_head=next; /* Publish only after all bytes have been stored. */
    return 1;
}
static void wg_tag(const unsigned char __xdata * __xdata frame)
{
    static __xdata unsigned char r,c;
    static __xdata unsigned long value;
    value=0;
    /* Drop EM4100 header, row/column parity and upper identifier bytes. */
    for(r=(wg_bits==26 ? 4 : 2);r<10;++r)
        for(c=0;c<4;++c) value=(value<<1)|frame_bit(frame,9+r*5+c);
    wg_enqueue(value);
}
static void pin_tick(void)
{
    if(pin_digits && (unsigned long)(reader_clock_ticks-pin_last)>=20000000UL)
        pin_clear();
}
static void pin_key(char key)
{
    static __xdata unsigned long quotient;
    static __xdata unsigned char remainder;
    static __xdata unsigned char digit;
    quotient=(wg_bits==26 ? 1677721UL : 429496729UL);
    remainder=5;
    pin_tick();
    if(key=='*') { pin_clear(); return; }
    if(key=='#') {
        if(pin_digits && !pin_invalid) {
            wg_enqueue(pin_value);
            indicator_submit(); /* Local entry feedback, not access approval. */
        }
        pin_clear(); return;
    }
    if(key<'0' || key>'9') return;
    digit=key-'0'; pin_last=reader_clock_ticks;
    if(pin_digits>=10 || pin_value>quotient ||
       (pin_value==quotient && digit>remainder)) pin_invalid=1;
    if(!pin_invalid) { pin_value=(pin_value<<3)+(pin_value<<1)+digit; ++pin_digits; }
    /* Invalid input remains latched until #, *, timeout or KEY CANCEL. */
}

#pragma save
#pragma nooverlay
static void wg_tick(void) __using(1)
{
    if(!wg_enabled) return;
    if(wg_wait && --wg_wait) return;
    if(wg_low) {
        WG_D0=!wg_active; WG_D1=!wg_active; wg_low=0;
        if(++wg_pos==wg_bits) {
            wg_tail=(wg_tail==2 ? 0 : wg_tail+1); wg_pos=0;
            /* ESPHome closes a frame after 100 ms without a bit. Allow
             * 150 ms idle from the final rising edge; round 32 us up. */
            wg_wait=(read_sample_ticks==32 ? 9375 : 4688);
        } else {
            wg_mask>>=1;
            if(!wg_mask) { wg_mask=0x80; ++wg_byte; }
            wg_wait=(read_sample_ticks==32 ? 101 : 51);
        }
        return;
    }
    if(wg_tail==wg_head) return;
    if(!wg_pos) { wg_mask=0x80; wg_byte=0; }
    if(wg_frames[wg_tail][wg_byte]&wg_mask) WG_D1=wg_active;
    else WG_D0=wg_active;
    wg_low=1; wg_wait=(read_sample_ticks==32 ? 6 : 3); /* nominal 96 us */
}
#pragma restore

/* Stopped-mode test uses the proven low-when-grounded sense inputs.
 * Each drive lasts 1 ms, separated by 20 ms high-Z. No UART while driving. */
static void wg_probe_wait(unsigned int ticks)
{
    static __xdata unsigned int start;
    start=timer_read();
    while((unsigned int)(timer_read()-start)<ticks);
}
static unsigned char wg_probe_level(unsigned char mask,unsigned char high)
{
    static __xdata unsigned char sense;
    if(high) P5|=mask; else P5&=(unsigned char)~mask;
    P5M0|=mask; P5M1&=(unsigned char)~mask;
    wg_probe_wait(2000);
    sense=P3&0x0c;
    P5M1|=0x30; P5M0&=0xcf;
    wg_probe_wait(40000);
    if((P3&0x0c)!=0x0c) return 255;
    return sense;
}
static unsigned char wg_probe_polarity(unsigned char a,unsigned char b,
                                       unsigned char c,unsigned char d)
{
    if(a==4 && b==12 && c==8 && d==12) return 1;
    if(a==12 && b==4 && c==12 && d==8) return 0;
    return 255;
}
static void wg_probe(void)
{
    static __xdata unsigned char a,b,c,d,polarity;
    wg_enabled=0; wg_reset(); timer_start();
    wg_probe_wait(40000);
    if((P3&0x0c)!=0x0c) { uart_puts("ERR WG IDLE LOW\r\n"); }
    else {
        a=wg_probe_level(0x10,1); b=wg_probe_level(0x10,0);
        c=wg_probe_level(0x20,1); d=wg_probe_level(0x20,0);
        uart_puts("WG SENSE D0_HIGH/LOW D1_HIGH/LOW: ");
        hex8(a); uart_putc(' '); hex8(b); uart_putc(' ');
        hex8(c); uart_putc(' '); hex8(d); uart_puts("\r\n");
        polarity=wg_probe_polarity(a,b,c,d);
        if(polarity!=255) wg_enable(polarity);
        if(wg_enabled) {
            uart_puts("OK WG ACTIVE="); uart_putc('0'+wg_active); uart_puts("\r\n");
        } else uart_puts("ERR WG POLARITY; OUTPUT DISABLED\r\n");
    }
    T4T3M&=~0x08;
}
#endif
