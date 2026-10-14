#ifndef RFID_HEADER_SAMPLER_H
#define RFID_HEADER_SAMPLER_H
/* Foreground-only RF/64 sampler. Four fixed bit phases, 128 us apart.
 * Integrate the central 128 us of each 256 us half-bit. No edge width
 * classification, payload search, parity repair, allocation or raw waveform buffer.
 * Qualified edge FIFO remains the sole ISR/foreground boundary.
 */
static __xdata unsigned int header_previous, header_since_valid;
static __xdata unsigned char header_seen, header_published;
static __xdata unsigned char header_locked=255, header_lock_bits;

static void header_reset(void)
{
    header_seen=header_published=0;
    header_locked=255;
}

static void header_bit(unsigned char channel)
{
    header_phase __xdata *p;
    static __xdata unsigned char bit,j,k,value;
    static __xdata unsigned int difference, error;
    p=&header_phases[channel];
    /* Follow a validated phase; return to search after 64 ms without another
     * complete frame. This deadline is counted even on ambiguous bits. */
    if(header_locked!=255 && ++header_lock_bits>=128) {
        header_locked=255; header_seen=0; return;
    }
    /* Choose the better of the two Manchester templates from measured area.
     * Equal costs are ambiguous. Parity never changes the chosen bit. */
    if(p->first==p->area) {
        p->count[0]=p->count[1]=p->run=0; return;
    }
    bit=p->first>p->area;
    difference=bit ? p->first-p->area : p->area-p->first;
    error=256-difference;
    if(!p->run || bit!=p->polarity) {
        p->polarity=bit; p->run=1; p->run_error=error;
    } else if(p->run<10) { ++p->run; p->run_error+=error; }
    /* Independent polarity candidates prevent long zero payloads from hiding
     * the real header. No 64-bit candidate blocks subsequent header searches. */
    for(k=0;k<2;++k) {
      value=bit^k;
      if(p->run==9 && value) {
        p->frame[k][0]=0xff; p->frame[k][1]=0x80;
        p->count[k]=9; p->error[k]=p->run_error;
        continue;
      }
      if(!p->count[k]) continue;
      p->error[k]+=error;
      /* 6.25% central-window disagreement cap; not a measured error rate. */
      if(p->error[k]>2048) { p->count[k]=0; continue; }
      j=p->count[k]>>3;
      if(!(p->count[k]&7)) p->frame[k][j]=0;
      p->frame[k][j]|=value<<(7-(p->count[k]&7));
      ++p->count[k];
      /* Preserve the existing presence-only prefix policy. A prefix cannot
       * create a credential or a beep; full validation below is mandatory. */
      if(p->count[k]==32 && p->error[k]<=1024 && !reader_prefix_ready) {
          for(j=0;j<4;++j) reader_prefix[j]=p->frame[k][j];
          reader_prefix_ready=1;
      }
      if(p->count[k]==64) {
        p->count[k]=0;
        /* Adjacent sampling phases can identify the same physical frame.
         * Suppress only duplicate publications within 2 ms, not repeat IDs. */
        if(frame_valid(p->frame[k]) && !reader_valid &&
           (!header_published || header_since_valid>4000)) {
            for(j=0;j<8;++j) reader_frames[0][j]=p->frame[k][j];
            reader_frame_count=reader_valid=1;
            header_since_valid=0; header_published=1;
            header_locked=channel; header_lock_bits=0;
        }
      }
    }
}

static void header_transition(unsigned int tick,unsigned char old_level)
{
    static __xdata unsigned char i;
    static __xdata unsigned int cursor,end,step;
    header_phase __xdata *p;
    if(header_seen && header_since_valid<60000u) {
        step=tick-header_previous;
        if(step>4000) header_since_valid=60000u;
        else header_since_valid+=step;
    }
    if(!header_seen || (unsigned int)(tick-header_previous)>2000) {
        header_locked=255;
        header_seen=1; header_previous=tick;
        for(i=0;i<4;++i) {
            p=&header_phases[i];
            p->deadline=tick+128+(unsigned int)i*256;
            p->stage=p->count[0]=p->count[1]=p->run=0; p->area=p->first=0;
        }
        return;
    }
    for(i=0;i<4;++i) {
        if(header_locked!=255 && i!=header_locked) continue;
        p=&header_phases[i]; cursor=header_previous;
        /* At most eight boundaries per phase for a <=1 ms interval. */
        while((int)(tick-p->deadline)>=0) {
            end=p->deadline; step=end-cursor;
            if((p->stage&1) && old_level) p->area+=step;
            if(p->stage==1) { p->first=p->area; p->area=0; }
            else if(p->stage==3) {
                header_bit(i); p->area=0;
                if(!header_seen) { header_previous=tick; return; }
            }
            p->stage=(p->stage+1)&3; p->deadline+=256; cursor=end;
        }
        if((p->stage&1) && old_level) p->area+=(unsigned int)(tick-cursor);
    }
    header_previous=tick;
}
#endif
