#ifndef KEYPAD_SERVICE_H
#define KEYPAD_SERVICE_H
/* One release/select or sample/release step per millisecond. Full scan >=8 ms,
 * five equal scans debounce a press/release. No timer ownership or busy wait. */
#define P0PU (*(volatile __xdata unsigned char *)0xfe10)
#define P1PU (*(volatile __xdata unsigned char *)0xfe11)
#define P0IE (*(volatile __xdata unsigned char *)0xfe30)
#define P1IE (*(volatile __xdata unsigned char *)0xfe31)
static const __code char keypad_keys[]="123456789*0#";
static const __code unsigned char keypad_rows[]={0xfe,0xfd,0xfb,0xf7};
static __xdata unsigned char keypad_row,keypad_selected,keypad_held,keypad_candidate;
static __xdata unsigned char keypad_debounce,keypad_blocked,keypad_beeps,keypad_was_beeping;
static __xdata unsigned char keypad_gap;
static __xdata unsigned int keypad_last,keypad_raw,keypad_beep_end;

static void keypad_beep_request(void)
{
    if(keypad_beeps!=255) ++keypad_beeps;
    else ++reader_tx_dropped; /* Bounded queue; exposed in existing diagnostics. */
}
static void keypad_event(unsigned char down,unsigned char index)
{
    if(down) { pin_key(keypad_keys[index]); keypad_beep_request(); }
    if(!uart_reserve(16)) return;
    uart_puts(down ? "KEY DOWN " : "KEY UP ");
    uart_putc(keypad_keys[index]); uart_puts("\r\n");
}
static void keypad_update(unsigned int raw)
{
    static __xdata unsigned char next,i;
    static __xdata unsigned int bits;
    next=255;
    if(raw && (raw & (raw-1))) { keypad_blocked=1; pin_clear(); }
    if(keypad_blocked) {
        if(keypad_held!=255) {
            /* Ambiguous multi-key state cancels, never creates another press. */
            if(uart_reserve(16)) uart_puts("KEY CANCEL\r\n");
            keypad_held=255;
        }
        if(raw) keypad_debounce=0;
        else if(++keypad_debounce>=5) {
            keypad_blocked=keypad_debounce=0; keypad_candidate=255;
        }
        return;
    }
    bits=raw;
    for(i=0;i<12;++i) { if(bits&1) { next=i; break; } bits>>=1; }
    if(next!=keypad_candidate) { keypad_candidate=next; keypad_debounce=1; }
    else if(keypad_debounce<5) ++keypad_debounce;
    if(keypad_debounce>=5 && keypad_held!=keypad_candidate) {
        if(keypad_held!=255) keypad_event(0,keypad_held);
        keypad_held=keypad_candidate;
        if(keypad_held!=255) keypad_event(1,keypad_held);
    }
}
static void keypad_reset(unsigned int now)
{
    P0|=0x0f;
    keypad_row=keypad_selected=keypad_debounce=keypad_blocked=0;
    keypad_raw=keypad_beeps=keypad_was_beeping=keypad_gap=0;
    keypad_held=keypad_candidate=255;
    keypad_last=now; keypad_beep_end=now-40000u;
}
static void keypad_tick(unsigned int now)
{
    static __xdata unsigned char columns;
    if(keypad_was_beeping && !beep_active) {
        keypad_beep_end=now; keypad_gap=1;
        if(uart_reserve(16)) uart_puts("BEEP DONE\r\n");
    }
    keypad_was_beeping=beep_active;
    if(keypad_gap && (unsigned int)(now-keypad_beep_end)>=40000u) keypad_gap=0;
    if(!beep_active && keypad_beeps && !keypad_gap) {
        --keypad_beeps; beep_start(); keypad_was_beeping=1;
    }
    if((unsigned int)(now-keypad_last)<2000u) return;
    keypad_last=now;
    if(!keypad_selected) {
        P0|=0x0f; P0 &= keypad_rows[keypad_row]; keypad_selected=1;
    } else {
        columns=P1; P0|=0x0f; keypad_selected=0;
        if(!(columns&0x20)) keypad_raw|=1u<<(keypad_row*3);
        if(!(columns&0x10)) keypad_raw|=2u<<(keypad_row*3);
        if(!(columns&0x08)) keypad_raw|=4u<<(keypad_row*3);
        if(++keypad_row==4) { keypad_row=0; keypad_update(keypad_raw); keypad_raw=0; }
    }
}
static void keypad_hardware_init(void)
{
    static __xdata unsigned char saved;
    P0M1|=0x0f; P0M0&=0xf0; P0|=0x0f; P0M0|=0x0f;
    P1M1|=0x38; P1M0&=0xc7; P1|=0x38;
    saved=P_SW2; P_SW2|=0x80;
    P0PU|=0x0f; P1PU|=0x38; P0IE|=0x0f; P1IE|=0x38;
    P_SW2=saved;
}
#endif
