"""Exercise the real sampler/FIFO/decoder with delayed foreground processing.

Synthetic RF/64 Manchester input is sampled every 16 us, with up to 1 ms
between foreground drains. This tests data flow, not STC peripheral timing.
"""
import os
import sys
import re
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[1]
from sdcc_test_support import tool, compiler_environment
env = compiler_environment()


def frame(payload):
    bits = [1] * 9
    cols = [0] * 4
    for value in payload:
        for nibble in (value >> 4, value & 15):
            row = [(nibble >> (3 - i)) & 1 for i in range(4)]
            bits.extend(row + [sum(row) & 1])
            cols = [a ^ b for a, b in zip(cols, row)]
    return bits + cols + [0]


fixtures, calls = [], []
for index, (payload, invert, corrupt) in enumerate([
    (bytes.fromhex('0102030405'), 0, False),
    (bytes.fromhex('A5C3097E12'), 1, False),
    (bytes(5), 0, False),
    (bytes([255] * 5), 1, False),
    (bytes.fromhex('0102030405'), 0, True),
]):
    bits = frame(payload)
    if corrupt:
        bits[13] ^= 1
    half_levels = [v ^ invert for bit in bits * 4 for v in (bit, 1 - bit)]
    # 256 us per half-bit, 16 samples each; occasional one-sample glitches.
    levels = [v for v in half_levels for _ in range(16)]
    levels += levels[:32]  # Following header transitions finish the last stop bit.
    for i in range(87, len(levels), 317):
        # Avoid real edges; a single wrong sample must be filtered away.
        if levels[i - 3:i + 4] == [levels[i]] * 7:
            levels[i] ^= 1
    # Pack input samples to fit the 8051 code address space.
    packed = [sum(levels[i + j] << j for j in range(8))
              for i in range(0, len(levels), 8)]
    fixtures.append(f'static const __code unsigned char f{index}[]={{'
                    + ','.join(map(str, packed)) + '};')
    expected = frame(payload)
    packed_frame = [sum(expected[i + j] << (7 - j) for j in range(8))
                    for i in range(0, 64, 8)]
    fixtures.append(f'static const __code unsigned char e{index}[]={{'
                    + ','.join(map(str, packed_frame)) + '};')
    for stride in (1,2):
        calls.append(f'run_fixture(f{index}, e{index}, {len(levels)}, {int(corrupt)}, {stride});')

source = r'''
#define main firmware_main
#include "rfid_firmware_test.c"
#undef main
volatile __xdata __at(0x1000) unsigned char result[64];
static __xdata unsigned int matches, notifications;
static const unsigned char __code * __xdata expected;
void check(unsigned char ok) { ++result[0]; if(!ok) { ++result[1]; result[4+result[1]]=result[0]; } }
void finished(void) { while(1); }
void drain(void) {
    unsigned char j,ok;
    while(sample_tail!=sample_head) {
        decoder_transition(sample_time[sample_tail],sample_old[sample_tail]);
        sample_tail=(sample_tail+1)&127;
        if(reader_prefix_ready) {
            presence_observed_prefix(reader_prefix,reader_clock_ticks);
            reader_prefix_ready=0;
        }
        if(reader_valid) {
            ++matches; ok=1;
            for(j=0;j<8;++j) if(reader_frames[reader_frame_count-1][j]!=expected[j]) ok=0;
            if(!ok) ++result[1];
            notifications+=presence_acquired(reader_frames[reader_frame_count-1],reader_clock_ticks);
            reader_frame_count=reader_valid=reader_full=0;
        } else if(reader_full) reader_frame_count=reader_full=0;
    }
}
void run_fixture(const unsigned char __code *samples,
                 const unsigned char __code *want,
                 unsigned int count,unsigned char corrupt,unsigned char stride) __reentrant {
    static __xdata unsigned int i,now;
    matches=notifications=0; expected=want;
    sample_gap_limit=32u*stride+16u;
    sampler_reset(65000u,samples[0]&1);
    reader_reset(65000u,samples[0]&1); presence_reset();
    reader_clock_previous=65000u; reader_clock_ticks=0;
#ifdef K7612_KEYPAD
    keypad_reset(65000u);
#endif
    reader_uart_active=1; reader_tx_head=reader_tx_tail=reader_tx_busy=0;
    reader_tx_dropped=0; beep_start();
    for(i=stride;i<count;i+=stride) {
        now=65000u+i*32u;
        sampler_tick(now,(samples[i>>3]>>(i&7))&1);
        reader_clock_update(now);
#ifdef K7612_KEYPAD
        P1=(keypad_selected && keypad_row==0) ? 0xdf : 0xff;
        keypad_tick(now);
#endif
        if(!(i&63)) {
            /* UART traffic and buzzer servicing accompany delayed decoding. */
            uart_putc('X'); TI=1; uart1_isr(); timer4_isr();
            drain();
        }
    }
    drain();
    result[20+result[0]/4]=matches;
    check(!sample_fault && sample_max_gap==32u*stride);
    check(corrupt ? matches==0 : matches>=3);
    check(notifications==(corrupt ? 0 : 1));
    check(!reader_tx_dropped);
#ifdef K7612_KEYPAD
    check(keypad_held==0);
#endif
    beep_stop(); reader_uart_active=0; ES=0;
}
''' + '\n'.join(fixtures) + '''
void main(void) {
    initialize_hardware(); result[0]=result[1]=result[2]=0;
''' + ('reader_clock_tracking=2;\n' if '--header' in sys.argv else 'reader_clock_tracking=1;\n' if '--clock-tracking' in sys.argv else 'reader_clock_tracking=0;\n') + '\n'.join(calls) + '\nresult[2]=0xa5; finished();}\n'

if '--combined' in sys.argv:
    source='#define K7612_KEYPAD 1\n'+source
with tempfile.TemporaryDirectory(prefix='rfid-streaming-') as directory:
    folder = Path(directory)
    (folder / 'test.c').write_text(source)
    subprocess.run([str(tool('sdcc')), '-mmcs51', '--std-sdcc11',
                    '-I' + str(root / 'src'), 'test.c'], cwd=folder, env=env,
                   check=True)
    address = re.search(r'\b([0-9A-Fa-f]{8})\s+_finished\b',
                        (folder / 'test.map').read_text()).group(1)
    run = subprocess.run([str(tool('s51')), '-q', '-c', '-', 'test.ihx'],
                         cwd=folder, env=env,
                         input=f'run 0 0x{address}\ndump xram 0x1000 0x101f\nquit\n',
                         text=True, capture_output=True, timeout=60, check=True)
    checks=50 if '--combined' in sys.argv else 40
    if not re.search(rf'0x1000\s+{checks:02x} 00 a5', run.stdout, re.I):
        raise AssertionError(run.stdout + run.stderr)
    print(f'PASS: {checks} streaming checks (16/32 us): sampled glitches, delayed decoding, UART, beep, parity, timer wrap'+('; simultaneous keypad scan' if '--combined' in sys.argv else ''))
