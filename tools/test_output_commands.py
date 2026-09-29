"""Compile the real command parser with SDCC and exercise it in the 8051 simulator."""
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
bin_dir = Path.home() / '.platformio/packages/toolchain-sdcc/bin'
environment = dict(os.environ, PATH=str(bin_dir) + os.pathsep + os.environ['PATH'])
cases = []
for index, name in enumerate(('LED1', 'LED2', 'BUZZER')):
    for token, value in [('Y', 1), ('N', 0)]:
        cases.append((f'{name} {token}', 1, index, value))
    for token, value in [('H', 1), ('L', 0)]:
        cases.append((f'POL {name} {token}', 2, index, value))
cases += [('  led1\t y  ', 1, 0, 1), ('buzzer n', 1, 2, 0),
          ('RELEASE', 3, 99, 99), ('?', 5, 99, 99),
          ('DIAG Y', 4, 99, 1), ('DIAG N', 4, 99, 0)]
for text in ['', ' ', 'LED1', 'LED1 YES', 'LED1 Y EXTRA', 'LED1 X',
             'LED3 Y', 'BUZZER YY', 'POL LED1', 'POL LED1 HIGH',
             'POL LED3 H', 'POL LED1 H EXTRA', 'RELEASE Y', '? EXTRA',
             'DIAG Y EXTRA', 'LED1Y', 'POL LED1 X']:
    cases.append((text, 0, 99, 99))
calls = '\n'.join(f'check({json.dumps(t)}, {kind}, {index}, {value});'
                  for t, kind, index, value in cases)
source = '''
#include "output_commands.h"
volatile __xdata __at(0x1000) unsigned char result[3];
char line[64];
void check(const char *text, unsigned char expected, unsigned char ei, unsigned char ev) {
    unsigned char target=99, value=99, actual;
    strcpy(line,text);
    actual=parse_command(line,&target,&value);
    ++result[0];
    if (actual!=expected || target!=ei || value!=ev) ++result[1];
}
void finished(void) { while(1); }
void main(void) {
    result[0]=0; result[1]=0; result[2]=0;
''' + calls + '\nresult[2]=0xa5; finished();\n}\n'
with tempfile.TemporaryDirectory(prefix='k7612-parser-') as directory:
    folder = Path(directory)
    (folder / 'test.c').write_text(source)
    subprocess.run([str(bin_dir / 'sdcc.exe'), '-mmcs51', '--std-sdcc11',
                    '-I' + str(root / 'src'), 'test.c'], cwd=folder, check=True,
                   text=True, env=environment)
    symbols = (folder / 'test.map').read_text()
    address = re.search(r'\b([0-9A-Fa-f]{8})\s+_finished\b', symbols).group(1)
    commands = f'run 0 0x{address}\ndump xram 0x1000 0x1002\nquit\n'
    run = subprocess.run([str(bin_dir / 's51.exe'), '-q', '-c', '-', 'test.ihx'],
                         cwd=folder, input=commands, capture_output=True,
                         text=True, timeout=15, check=True)
    expected = f'{len(cases):02x} 00 a5'
    if not re.search(r'0x1000\s+' + expected, run.stdout, re.IGNORECASE):
        raise AssertionError(run.stdout + run.stderr)
    print(f'PASS: {len(cases)} real-parser cases in the 8051 simulator')
