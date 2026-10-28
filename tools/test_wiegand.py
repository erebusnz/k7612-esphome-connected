"""Compile actual reader; check framing, output scheduling and PIN boundaries in s51."""
from pathlib import Path
import re
import subprocess
import tempfile
from sdcc_test_support import tool, compiler_environment

root = Path(__file__).resolve().parents[1]
source = r'''
#define main firmware_main
#include "keypad_reader.c"
#undef main
volatile __xdata __at(0x1000) unsigned char result[64];
static __xdata unsigned char i,j,k,expected,leading,trailing,last,edges;
static __xdata unsigned int tick,fall,rise;
static __xdata unsigned long value;
static __xdata unsigned char frame[8];
void check(unsigned char ok) { ++result[0]; if(!ok && result[1]<59) result[4+result[1]++]=result[0]; }
void finished(void) { while(1); }
void enter(const __code char *s) { while(*s) pin_key(*s++); }
void main(void) {
    wg_bits=157; /* Hardware bring-up exposed unreliable initial format state. */
    initialize_hardware(); EA=0;
    result[0]=result[1]=0; reader_clock_ticks=0;
    check(wg_bits==K7612_WIEGAND_BITS);
    check(!wg_enabled && (P5M1&0x30)==0x30 && !(P5M0&0x30));
    check(!wg_enqueue(1234));
    check(wg_probe_polarity(4,12,8,12)==1);
    check(wg_probe_polarity(12,4,12,8)==0);
    check(wg_probe_polarity(0,12,8,12)==255);
    check(wg_probe_polarity(4,12,12,8)==255);
    check(wg_probe_polarity(255,12,8,12)==255);
    /* All payload bit positions, both formats, independent parity check. */
    for(k=0;k<2;++k) {
        wg_bits=k ? 34 : 26; wg_enable(0);
        for(j=0;j<wg_bits-2;++j) {
            wg_reset(); value=1UL<<j; wg_enqueue(value);
            leading=trailing=0;
            for(i=0;i<wg_bits;++i) {
                expected=(wg_frames[0][i>>3]>>(7-(i&7)))&1;
                if(i<wg_bits/2) leading^=expected; else trailing^=expected;
                if(i && i<wg_bits-1 && expected!=((value>>(wg_bits-2-i))&1)) result[1]++;
            }
            check(!leading && trailing==1);
        }
    }
    wg_bits=26; wg_reset(); enter("1234"); check(wg_head==wg_tail && pin_value==1234);
    enter("#"); check(wg_head!=wg_tail && !pin_digits);
    check(wg_frames[0][0]==0 && wg_frames[0][1]==2 && wg_frames[0][2]==0x69 && wg_frames[0][3]==0);
    wg_reset(); enter("#"); check(wg_head==wg_tail);
    enter("123*#"); check(wg_head==wg_tail);
    enter("001234#"); check(wg_head!=wg_tail && wg_frames[0][2]==0x69);
    wg_reset(); enter("16777215#"); check(wg_head!=wg_tail);
    wg_reset(); enter("16777216#"); check(wg_head==wg_tail);
    enter("999999999999999#"); check(wg_head==wg_tail);
    enter("0#"); check(wg_head!=wg_tail);
    wg_bits=34; wg_reset(); enter("4294967295#"); check(wg_head!=wg_tail);
    wg_reset(); enter("4294967296#"); check(wg_head==wg_tail);
    enter("123"); reader_clock_ticks=20000000UL; pin_tick(); enter("#"); check(wg_head==wg_tail);
    reader_clock_ticks=0xfffffff0UL; enter("123"); reader_clock_ticks+=20000000UL;
    pin_tick(); check(!pin_digits);
    keypad_reset(0); enter("123"); keypad_update(3); check(!pin_digits);
    wg_reset(); check(wg_enqueue(1)); check(wg_enqueue(2)); check(!wg_enqueue(3));
    wg_reset(); check(wg_head==wg_tail && WG_D0 && WG_D1);
    /* Actual pin writes: both polarities, both sample rates, two frames FIFO. */
    for(k=0;k<4;++k) {
        wg_bits=(k&1) ? 34 : 26; wg_enable(k&1);
        read_sample_ticks=(k&2) ? 64 : 32;
        wg_enqueue(1234); wg_enqueue(0xabcdefUL);
        edges=last=0; fall=rise=0;
        for(tick=0;tick<12000;++tick) {
            wg_tick();
            i=(WG_D0==wg_active); j=(WG_D1==wg_active);
            if(i && j) result[1]++;
            if((i||j) && !last) {
                if(edges && edges!=wg_bits && tick-fall!=((k&2)?54:107)) result[1]++;
                if(edges==wg_bits && tick-rise!=((k&2)?1563:3125)) result[1]++;
                value=edges<wg_bits ? 1234 : 0xabcdefUL;
                expected=edges%wg_bits;
                if(!expected) expected=wg_parity(value>>((wg_bits-2)/2));
                else if(expected==wg_bits-1) expected=1^wg_parity(value&((1UL<<((wg_bits-2)/2))-1));
                else expected=(value>>(wg_bits-2-expected))&1;
                if(j!=expected) result[1]++;
                ++edges; fall=tick;
            }
            if(!(i||j) && last) {
                if(tick-fall!=((k&2)?3:6)) result[1]++;
                rise=tick;
            }
            last=i||j;
        }
        check(edges==2*wg_bits && !wg_low && wg_head==wg_tail);
    }
    /* Construct EM4100 data 0102A55AC3; wg_tag must strip row parity. */
    for(i=0;i<8;++i) frame[i]=0;
    value=0x02a55ac3UL;
    for(i=0;i<32;++i) {
        j=9+(i/4+2)*5+i%4;
        if((value>>(31-i))&1) frame[j>>3]|=0x80>>(j&7);
    }
    wg_bits=26; wg_reset(); wg_tag(frame);
    check(wg_frames[0][0]==0x52 && wg_frames[0][1]==0xad && wg_frames[0][2]==0x61 && wg_frames[0][3]==0xc0);
    wg_bits=34; wg_reset(); wg_tag(frame);
    check(wg_frames[0][0]==0x81 && wg_frames[0][1]==0x52 && wg_frames[0][2]==0xad && wg_frames[0][3]==0x61 && wg_frames[0][4]==0xc0);
    result[2]=0xa5; finished();
}
'''
with tempfile.TemporaryDirectory(prefix='wiegand-') as directory:
    folder = Path(directory)
    (folder/'test.c').write_text(source)
    subprocess.run([str(tool('sdcc')), '-mmcs51', '--std-sdcc11', '-I'+str(root/'src'), 'test.c'], cwd=folder, env=compiler_environment(), check=True)
    address = re.search(r'\b([0-9A-Fa-f]{8})\s+_finished\b', (folder/'test.map').read_text())[1]
    run = subprocess.run([str(tool('s51')), '-q', '-c', '-', 'test.ihx'], cwd=folder, env=compiler_environment(), input=f'run 0 0x{address}\ndump xram 0x1000 0x103f\nquit\n', text=True, capture_output=True, timeout=60, check=True)
    match = re.search(r'0x1000\s+([0-9a-f]{2}) 00 a5', run.stdout, re.I)
    assert match, run.stdout+run.stderr
    print(f'PASS: {int(match[1],16)} Wiegand/PIN checks plus bit and pulse assertions')
