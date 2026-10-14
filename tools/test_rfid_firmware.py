"""Exercise real RFID initialization, carrier controls and capture logic in SDCC/s51.

The generic 8051 simulator cannot validate STC timer/comparator peripherals or RF.
"""
import os
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
from sdcc_test_support import tool, compiler_environment
env = compiler_environment()
source = r'''
#define main firmware_main
#include "rfid_firmware_test.c"
#undef main
volatile __xdata __at(0x1000) unsigned char result[3];
void check(unsigned char ok) { ++result[0]; if (!ok) ++result[1]; }
void finished(void) { while (1); }
void check_presence(void) {
    unsigned int i;
    for(i=0;i<8;++i) reader_frames[0][i]=i;
    presence_reset();
    check(presence_acquired(reader_frames[0],0));
    check(!presence_acquired(reader_frames[0],400000UL));
    check(!presence_acquired(reader_frames[0],800000UL));
    for(i=1;i<=30;++i)
        if(presence_acquired(reader_frames[0],800000UL+i*1000000UL)) ++result[1];
    check(presence_notified && presence_count==1); /* Held for >10 s. */
    presence_expire(33799999UL); check(presence_count==1);
    presence_expire(33800000UL); check(presence_count==0);
    check(presence_acquired(reader_frames[0],33800000UL));
    check(!presence_acquired(reader_frames[0],34200000UL));
    reader_frames[0][0]^=1;
    check(presence_acquired(reader_frames[0],34600000UL));
    check(!presence_acquired(reader_frames[0],35000000UL));
    presence_reset();
    check(presence_acquired(reader_frames[0],0xfffffff0UL));
    check(!presence_acquired(reader_frames[0],0x100UL));
    presence_expire(0x100UL+3000000UL); check(presence_count==0);
    reader_clock_previous=65530u; reader_clock_ticks=0;
    reader_clock_update(4u); check(reader_clock_ticks==10);
    reader_clock_update(1004u); check(reader_clock_ticks==1010);
    presence_reset();
    check(!presence_observed_prefix(reader_frames[0],0) && !presence_count);
    check(presence_acquired(reader_frames[0],0));
    check(presence_observed_prefix(reader_frames[0],2999999UL));
    check(!presence_expire(3000000UL));
    reader_frames[0][3]^=1;
    check(!presence_observed_prefix(reader_frames[0],4000000UL));
    reader_frames[0][3]^=1;
    check(!presence_observed_prefix(reader_frames[0],5999999UL));
    check(presence_expire(5999999UL));
    check(!presence_observed_prefix(reader_frames[0],6000000UL));
    check(presence_acquired(reader_frames[0],6000000UL));
}
void main(void) {
    unsigned int i;
    result[0]=0; result[1]=0; result[2]=0;
    initialize_hardware();
    check(!TR0 && !(INTCLKO & 1) && !P3_5);
    check(P3M1 == 0xcd && P3M0 == 0x32 && P3_4);
    check(P2M1 == 0xff && P2M0 == 0); /* Relay/LED pins released. */
    check(CMPCR1 == 0x84 && CMPCR2 == 0); /* No CMPO pin or interrupts. */
    check(!(P3PU & 0xe0));
    carrier_on();
    check(TR0 && (INTCLKO & 7) == 1);
    check((TMOD & 15) == 0 && (AUXR & 0x80));
    carrier_off();
    check(!TR0 && !(INTCLKO & 7) && !P3_5);
    capture_reset(65530, 0);
    capture_sample(4, 1);
    check(elapsed_ticks == 10 && edge_time[0] == 10 && edge_level[0] == 1);
    capture_sample(104, 1);
    check(edge_count == 1 && elapsed_ticks == 110 && max_gap == 100);
    capture_sample(204, 0);
    check(edge_count == 2 && edge_time[1] == 210 && edge_level[1] == 0);
    capture_reset(0, 0);
    for (i=1; i<=193; ++i) capture_sample(i, i & 1);
    check(edge_count == 192 && dropped == 1);
    check(edge_time[191] == 192 && edge_level[191] == 0);
    dropped=65535;
    capture_sample(194, 0);
    check(dropped == 65535);
    capture_reset(0, 1);
    check(!edge_count && !dropped && !elapsed_ticks && !max_gap && initial_level == 1);
    for (i=0; i<10; ++i) capture_sample((unsigned int)((i+1)*20000UL), 1);
    check(elapsed_ticks == 200000UL && !edge_count);
    check((AUXR&0x15)==0 && TR1 && !uart_timer2_ready);
    beep_start(); check(beep_active && beep_edges_left==1020 && P3_4);
    check(T4H==0xfe && T4L==0x70 && (T4T3M&0xf0)==0x80 && (IE2&0x40));
    for(i=0;i<1020;++i) {
        timer4_isr();
        if(P3_4 != (i&1)) ++result[1];
    }
    check(!beep_active && P3_4 && !(IE2&0x40) && !(T4T3M&0x80));
    beep_start(); timer4_isr(); beep_start(); check(beep_edges_left==1019);
    beep_stop(); check(!beep_active && !beep_edges_left && P3_4);
    check(!(INTCLKO & 7) && !TR0 && !(CMPCR1 & 2));
    sampler_reset(65520u,0);
    sampler_tick(16u,1); sampler_tick(48u,1); check(sample_head==0);
    sampler_tick(80u,1); check(sample_head==1 && sample_time[0]==16u && sample_old[0]==0);
    sampler_tick(112u,0); sampler_tick(144u,1); sampler_tick(176u,1);
    check(sample_head==1 && !sample_fault); /* One-sample glitch rejected. */
    sampler_tick(240u,1); check(sample_fault && sample_max_gap==64);
    sampler_reset(0,0);
    for(i=1;i<=384;++i) sampler_tick(i*32u, ((i-1)/3+1)&1);
    check(sample_fault && sample_head==127 && sample_tail==0); /* No overwrite. */
    check(sample_time[0]==32 && sample_time[126]==12128);
    sampler_reset(0,0); sample_head=sample_tail=127;
    sampler_tick(32,1); sampler_tick(64,1); sampler_tick(96,1);
    check(sample_head==0 && sample_time[127]==32 && sample_old[127]==0);
    sample_tail=0;
    sampler_tick(128,0); sampler_tick(160,0); sampler_tick(192,0);
    check(sample_head==1 && sample_time[0]==128 && sample_old[0]==1);
    reader_uart_active=1; reader_tx_head=reader_tx_tail=reader_tx_busy=0;
    reader_tx_dropped=0;
    uart_putc('A'); check(reader_tx_busy && reader_tx_head==1 && reader_tx_tail==1);
    uart_putc('B'); check(reader_tx_head==2 && reader_tx_tail==1);
    TI=1; uart1_isr(); check(reader_tx_tail==2 && reader_tx_busy);
    TI=1; uart1_isr(); check(!reader_tx_busy);
    for(i=0;i<256;++i) uart_putc(i);
    check(reader_tx_head==reader_tx_tail-1 && reader_tx_busy && !reader_tx_dropped);
    uart_putc(9); check(reader_tx_dropped==1);
    for(i=0;i<256;++i) { TI=1; uart1_isr(); }
    check(!reader_tx_busy && reader_tx_head==reader_tx_tail);
    reader_uart_active=0; ES=0;
    sampler_window=2; sampler_window_done=0; sampler_probe=0;
    TR1=1; ET1=1; carrier_on();
    timer1_isr(); check(sampler_window==1 && !sampler_window_done && TR1);
    timer1_isr(); check(!sampler_window && sampler_window_done && !TR1 && !ET1 && !TR0 && !(INTCLKO&1) && !P3_5);
    sampler_window_done=0;
    check_presence();
    result[2]=0xa5; finished();
}
'''
with tempfile.TemporaryDirectory(prefix='k7612-rfid-') as directory:
    folder = Path(directory)
    (folder / 'test.c').write_text(source)
    subprocess.run([str(tool('sdcc')), '-mmcs51', '--std-sdcc11',
                    '-I' + str(root / 'src'), 'test.c'], cwd=folder, env=env, check=True)
    address = re.search(r'\b([0-9A-Fa-f]{8})\s+_finished\b',
                        (folder / 'test.map').read_text()).group(1)
    run = subprocess.run([str(tool('s51')), '-q', '-c', '-', 'test.ihx'],
                         cwd=folder, env=env, input=f'run 0 0x{address}\ndump xram 0x1000 0x1002\nquit\n',
                         text=True, capture_output=True, timeout=20, check=True)
    match=re.search(r'0x1000\s+([0-9a-f]{2}) 00 a5', run.stdout, re.IGNORECASE)
    if not match:
        raise AssertionError(run.stdout + run.stderr)
    print(f'PASS: {int(match[1],16)} RFID hardware-state, sampler, UART, beep, presence and clock checks')
