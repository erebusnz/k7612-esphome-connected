"""Run the actual combined firmware keypad/beep state machines under SDCC/s51."""
import os,re,subprocess,tempfile
from pathlib import Path
root=Path(__file__).resolve().parents[1]
from sdcc_test_support import tool, compiler_environment
env = compiler_environment()
source=r'''
#define main firmware_main
#include "keypad_reader.c"
#undef main
volatile __xdata __at(0x1000) unsigned char result[64];
static __xdata unsigned char i,j;
static __xdata unsigned int now;
void check(unsigned char ok) { ++result[0]; if(!ok) result[4+result[1]++]=result[0]; }
void finished(void) { while(1); }
void main(void) {
    initialize_hardware(); EA=0;
    result[0]=result[1]=0;
    check(!reader_debug && (P3PU&1));
    reader_tx_head=reader_tx_tail=reader_tx_busy=0; reader_tx_dropped=0;
    SBUF=0x5a; TI=0;
    uart_puts("silent"); uart_putc('X');
    check(!TI && !reader_tx_busy && !reader_tx_head && !reader_tx_dropped);
    check(!uart_reserve(16) && !reader_tx_dropped);
    check(uart_use_timer2() && !TR1 && uart_timer2_ready && !TI);
    reader_debug=1;
    reader_uart_active=1; reader_tx_busy=1; reader_tx_head=reader_tx_tail=0;
    strcpy(command,"DEBUG OFF"); execute(); check(!reader_debug);
    j=reader_tx_head; uart_puts("suppressed"); check(reader_tx_head==j);
    strcpy(command,"DEBUG ON"); execute(); check(reader_debug && reader_tx_head!=j);
    reader_tx_head=reader_tx_tail=0;
    keypad_reset(0);
    for(i=0;i<12;++i) {
        for(j=0;j<4;++j) keypad_update(1u<<i);
        check(keypad_held==255 && keypad_beeps==0);
        keypad_update(1u<<i);
        check(keypad_held==i && keypad_beeps==1);
        for(j=0;j<20;++j) keypad_update(1u<<i);
        check(keypad_beeps==1);
        for(j=0;j<5;++j) keypad_update(0);
        check(keypad_held==255 && keypad_beeps==1);
        keypad_beeps=0; reader_tx_tail=reader_tx_head;
    }
    for(j=0;j<10;++j) { keypad_update(1); keypad_update(0); }
    check(keypad_beeps==0 && keypad_held==255);
    keypad_update(3);
    for(j=0;j<10;++j) keypad_update(1);
    check(keypad_blocked && keypad_beeps==0);
    for(j=0;j<5;++j) keypad_update(0);
    check(!keypad_blocked);
    for(j=0;j<5;++j) keypad_update(1);
    check(keypad_beeps==1 && keypad_held==0);
    keypad_update(3);
    check(keypad_held==255 && keypad_blocked && keypad_beeps==1);
    /* Real row scheduling across timestamp wrap; key 6 is row1 / column2. */
    keypad_reset(65000); now=65000; P0=0xaf;
    for(i=0;i<40;++i) {
        now+=2000; P1=(keypad_selected && keypad_row==1) ? 0xf7 : 0xff;
        keypad_tick(now);
        check((P0&0xf0)==0xa0);
    }
    check(keypad_held==5 && keypad_beeps==1);
    /* FIFO beep requests survive an active beep; silence interval wraps. */
    keypad_reset(65000); beep_stop();
    keypad_beep_request(); keypad_beep_request(); keypad_tick(65000);
    check(beep_active && keypad_beeps==1);
    beep_stop(); keypad_tick(65500);
    check(!beep_active && keypad_beeps==1 && keypad_gap);
    keypad_tick((unsigned int)(65500u+39999u));
    check(!beep_active && keypad_beeps==1);
    keypad_tick((unsigned int)(65500u+40000u));
    check(beep_active && keypad_beeps==0);
    beep_stop(); keypad_reset(0);
    check((P0&15)==15 && keypad_beeps==0);
    /* OPEN state, active-HIGH LED output, # only feedback, clock wrap. */
    reader_clock_ticks=0; P2_2=1; indicator_reset();
    check(P2_7 && !P2_6 && !indicator_flash);
    check((P2M1&0xc4)==0x04 && (P2M0&0xc4)==0xc0);
    P2_2=0; indicator_tick(); reader_clock_ticks=39999; indicator_tick();
    check(P2_7 && !P2_6);
    reader_clock_ticks=40000; indicator_tick(); check(!P2_7 && P2_6);
    keypad_reset(0); pin_clear();
    keypad_event(1,0); check(keypad_beeps==1 && !indicator_flash);
    keypad_event(0,0); check(keypad_beeps==1 && !indicator_flash);
    keypad_event(1,11); check(keypad_beeps==2 && indicator_flash && P2_7 && P2_6);
    reader_clock_ticks+=299999; indicator_tick(); check(P2_7 && P2_6);
    ++reader_clock_ticks; indicator_tick(); check(!P2_7 && P2_6 && !indicator_flash);
    keypad_event(1,11); check(!indicator_flash); /* Empty #. */
    pin_key('2'); pin_key('*'); pin_key('#'); check(!indicator_flash);
    pin_invalid=1; pin_digits=1; pin_key('#'); check(!indicator_flash);
    /* OPEN changes during flash are restored to the current stable state. */
    reader_clock_ticks=0xfffffff0UL; indicator_submit();
    P2_2=1; indicator_tick(); reader_clock_ticks+=40000UL; indicator_tick();
    check(indicator_open && P2_6 && P2_7);
    reader_clock_ticks+=260000UL; indicator_tick(); check(!P2_6 && P2_7);
    /* Contact bounce does not change the indication before 20 ms stability. */
    P2_2=0; indicator_tick(); reader_clock_ticks+=20000UL;
    P2_2=1; indicator_tick(); reader_clock_ticks+=20000UL; indicator_tick();
    check(!P2_6 && P2_7);
    indicator_submit(); indicator_reset(); check(!indicator_flash && P2_7 && !P2_6);
    /* A full UART queue must not prevent multi-key cancellation, and the
     * lost diagnostic must be counted exactly once without a partial write. */
    keypad_reset(0); keypad_held=0; pin_key('1');
    reader_tx_head=10; reader_tx_tail=11; reader_tx_busy=1; reader_tx_dropped=0;
    keypad_update(3);
    check(keypad_blocked && keypad_held==255 && !pin_digits);
    check(reader_tx_dropped==1 && reader_tx_head==10 && reader_tx_tail==11);
    keypad_update(3); check(reader_tx_dropped==1);
    result[2]=0xa5; finished();
}
'''
with tempfile.TemporaryDirectory(prefix='keypad-reader-') as d:
    folder=Path(d); (folder/'test.c').write_text(source)
    subprocess.run([str(tool('sdcc')),'-mmcs51','--std-sdcc11','-I'+str(root/'src'),'test.c'],cwd=folder,env=env,check=True)
    address=re.search(r'\b([0-9A-Fa-f]{8})\s+_finished\b',(folder/'test.map').read_text())[1]
    run=subprocess.run([str(tool('s51')),'-q','-c','-','test.ihx'],cwd=folder,env=env,input=f'run 0 0x{address}\ndump xram 0x1000 0x103f\nquit\n',text=True,capture_output=True,timeout=60,check=True)
    m=re.search(r'0x1000\s+([0-9a-f]{2}) 00 a5',run.stdout,re.I)
    assert m,run.stdout+run.stderr
    print(f'PASS: {int(m[1],16)} combined keypad/debounce/row/beep/OPEN/LED checks')
