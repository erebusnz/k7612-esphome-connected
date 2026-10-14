"""Interrupt the real foreground clock update to detect SDCC overlay collisions."""
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
from sdcc_test_support import tool, compiler_environment
env = compiler_environment()
source = r'''
#define main firmware_main
#include "rfid_firmware_test.c"
#undef main
volatile __xdata __at(0x1000) unsigned char result[3];
void finished(void) { while(1); }
void main(void) {
    EA=0; TR1=0; TF1=0; ES=0; IE2=0;
    sampler_reset(0,0); sampler_window=sampler_probe=0;
    CMPCR1=1; T3H=T3L=0;
    reader_clock_previous=100; reader_clock_ticks=0;
    ET1=1; EA=1;
    reader_clock_update(356);
    EA=0;
    result[0]=(reader_clock_ticks==256UL);
    result[1]=(reader_clock_previous==356);
    result[2]=(sample_candidate==1); /* Confirms the interrupt actually ran. */
    finished();
}
'''
if '--combined' in sys.argv:
    source='#define K7612_KEYPAD 1\n'+source
with tempfile.TemporaryDirectory(prefix='k7612-irq-') as directory:
    folder = Path(directory)
    (folder / 'test.c').write_text(source)
    subprocess.run([str(tool('sdcc')), '-mmcs51', '--std-sdcc11',
                    '-I' + str(root / 'src'), 'test.c'], cwd=folder, env=env, check=True)
    listing = (folder / 'test.rst').read_text()
    clock = listing.split('_reader_clock_update:', 1)[1]
    # Stop after both bytes of the foreground argument have been stored.
    instructions = re.findall(r'^\s+([0-9A-F]{6})\s+(?:[0-9A-F]{2} )+.*$', clock, re.M)
    interrupted_at = instructions[2]
    finished = re.search(r'\b([0-9A-Fa-f]{8})\s+_finished\b',
                         (folder / 'test.map').read_text()).group(1)
    commands = (f'run 0 0x{interrupted_at}\nset memory sfr 0x88 0x80\n'
                f'run 0x{interrupted_at} 0x{finished}\ndump xram 0x1000 0x1002\nquit\n')
    run = subprocess.run([str(tool('s51')), '-q', '-c', '-', 'test.ihx'],
                         cwd=folder, env=env, input=commands, text=True,
                         capture_output=True, timeout=20, check=True)
    if not re.search(r'0x1000\s+01 01 01', run.stdout):
        raise AssertionError(run.stdout + run.stderr)
    print('PASS: asynchronous sampler interrupt preserves foreground timestamp and clock')
