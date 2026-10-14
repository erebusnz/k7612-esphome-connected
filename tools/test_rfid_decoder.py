"""Run actual SDCC decoder against independently generated EM4100 fixtures."""
import os, re, subprocess, tempfile, sys
from pathlib import Path
root=Path(__file__).resolve().parents[1]
from sdcc_test_support import tool, compiler_environment
env = compiler_environment()
def frame(payload):
    rows=[]; cols=[0]*4
    for v in payload:
        for n in (v>>4,v&15):
            b=[(n>>(3-i))&1 for i in range(4)]
            rows += b+[sum(b)%2]
            cols=[a^c for a,c in zip(cols,b)]
    return [1]*9+rows+cols+[0]
def events(bits, invert=0, skip=0, half=512, skew=0):
    levels=[v^invert for b in bits*3 for v in (b,1-b)][skip:]
    t=65000; old=levels[0]; out=[]
    for i,v in enumerate(levels):
        if v!=old:
            out.append((t&65535,old)); old=v
        t+=half+(skew if i%2 else -skew)
    return out
cases=[]
for payload in [bytes.fromhex('0102030405'),bytes(5),bytes([255]*5),bytes.fromhex('A5C3097E12')]:
    for inv in (0,1):
        for skip in (0,1,17,73):
            cases.append((events(frame(payload),inv,skip,half=512 if '--header' in sys.argv else 500,skew=45), frame(payload)))
for n in (9,13,59,63):
    bad=frame(bytes.fromhex('0102030405'));bad[n]^=1
    cases.append((events(bad),None))
cases.append(([(20,0),(40,1)]+events(frame(bytes.fromhex('1122334455'))),frame(bytes.fromhex('1122334455'))))
if '--clock-tracking' in sys.argv:
    # Independent timing fixtures: frequency offset and bounded edge jitter.
    for half in (500,524):
        bits=frame(bytes.fromhex('B72A19E406'))
        ev=events(bits,half=half)
        ev=[((t+(0,64,-64,32)[i%4])&65535,level) for i,(t,level) in enumerate(ev)]
        cases.append((ev,bits))
body=[]; arrays=[]
for index,(ev,want) in enumerate(cases):
    body.append('reader_reset(0,0);')
    arrays.append('static const __code unsigned int fixture%d[][2]={'%index + ','.join('{%du,%d}'%(t,v) for t,v in ev) + '};')
    body.append(f'feed(fixture{index}, {len(ev)});')
    body.append(f'check(reader_valid=={int(want is not None)});')
    if want is not None:
        packed=[sum(want[j+k]<<(7-k) for k in range(8)) for j in range(0,64,8)]
        arrays.append(f'static const __code unsigned char expected{index}[8]={{'+','.join(map(str,packed))+'};')
        body.append(f'check_matches(expected{index});')
body += ['reader_reset(65000u,0); reader_sample(65010u,1); reader_sample(65020u,0); reader_sample(65080u,0); check(reader_edges==0);',
         'reader_sample(65530u,1); reader_sample(50u,1); check(reader_edges==1 && filter_level==1);']
source='''#include "rfid_decoder.h"
volatile __xdata __at(0x1800) unsigned char result[96];
void check(unsigned char ok) { ++result[0]; if(!ok) { ++result[1]; result[4+result[1]]=result[0]; } }
void finished(void) { while(1); }
void check_matches(const unsigned char __code *expected) {
    unsigned char j,ok=reader_frame_count!=0;
    if(ok) for(j=0;j<8;++j) if(reader_frames[reader_frame_count-1][j]!=expected[j]) ok=0;
    check(ok);
}
void feed(const unsigned int __code data[][2], unsigned int count) {
    unsigned int i;
    for(i=0;i<count && !reader_valid && !reader_full;++i) decoder_transition(data[i][0],data[i][1]);
}
'''+'\n'.join(arrays)+'''
void main(void) { result[0]=result[1]=result[2]=result[3]=0;
'''+ ('reader_clock_tracking=2;\n' if '--header' in sys.argv else 'reader_clock_tracking=1;\n' if '--clock-tracking' in sys.argv else 'reader_clock_tracking=0;\n') + '\n'.join(body)+'\nresult[2]=0xa5; finished();}\n'
with tempfile.TemporaryDirectory(prefix='rfid-decoder-') as d:
    folder=Path(d);(folder/'test.c').write_text(source)
    subprocess.run([str(tool('sdcc')),'-mmcs51','--std-sdcc11','-I'+str(root/'src'),'test.c'],cwd=folder,env=env,check=True,capture_output=True)
    address=re.search(r'\b([0-9A-Fa-f]{8})\s+_finished\b',(folder/'test.map').read_text()).group(1)
    run=subprocess.run([str(tool('s51')),'-q','-c','-','test.ihx'],cwd=folder,env=env,input=f'run 0 0x{address}\ndump xram 0x1800 0x183f\nquit\n',text=True,capture_output=True,timeout=60,check=True)
    match=re.search(r'0x1800\s+([0-9a-f]{2}) ([0-9a-f]{2}) a5 ([0-9a-f]{2})',run.stdout,re.I)
    if not match or int(match[2],16): raise AssertionError(run.stdout+run.stderr)
    print(f'PASS: {int(match[1],16)} decoder checks')
