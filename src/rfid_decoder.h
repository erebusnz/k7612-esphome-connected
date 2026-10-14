#ifndef RFID_DECODER_H
#define RFID_DECODER_H
/* Timestamp units: 0.5 us. Streaming, bounded memory, no known-payload match.
 * A transition must remain stable for 24 us. This rejects short chatter;
 * it is not an analog moving average. Timestamp the first observed change.
 */
typedef struct {
    unsigned char frame[8], count, first, have, ready;
} rfid_phase;
typedef struct {
    unsigned int deadline, area, first, error[2], run_error;
    unsigned char frame[2][8], stage, count[2], run, polarity;
} header_phase;
/* Only one decoder runs at a time. Share candidate storage across modes. */
static __xdata union {
    rfid_phase widths[4];
    header_phase headers[4];
} decoder_state;
#define phases (decoder_state.widths)
#define header_phases (decoder_state.headers)
static __xdata unsigned char reader_frames[4][8];
static __xdata unsigned char reader_frame_count, reader_valid, reader_full;
static __xdata unsigned char reader_prefix[4];
static __xdata unsigned char reader_prefix_ready;
static __xdata unsigned char filter_level, filter_candidate, half_parity, edge_seen;
static __xdata unsigned int filter_since, decoder_edge, reader_edges, reader_resets;
static unsigned int decoder_clock;
/* 0: interval widths; 1: edge phase tracker; 2: header/window sampler. */
static unsigned char reader_clock_tracking=2;

static void decoder_sync_reset(void)
{
    unsigned char i, j;
    for (i=0; i<4; ++i) {
        phases[i].count=phases[i].have=phases[i].ready=0;
        for(j=0;j<8;++j) phases[i].frame[j]=0;
    }
    half_parity=0;
}
static unsigned char frame_bit(const unsigned char __xdata *f, unsigned char n)
{
    return (f[n >> 3] >> (7-(n & 7))) & 1;
}
static unsigned char frame_valid(const unsigned char __xdata *f)
{
    unsigned char r,c,p;
    if(f[0]!=0xff || !(f[1]&0x80) || (f[7]&1)) return 0;
    for(r=0;r<10;++r) {
        p=0;
        for(c=0;c<5;++c) p ^= frame_bit(f,9+r*5+c);
        if(p) return 0;
    }
    for(c=0;c<4;++c) {
        p=frame_bit(f,59+c);
        for(r=0;r<10;++r) p ^= frame_bit(f,9+r*5+c);
        if(p) return 0;
    }
    return 1;
}
#include "rfid_header_sampler.h"
static void decoder_bit(unsigned char channel, unsigned char bit)
{
    unsigned char j;
    rfid_phase __xdata *p=&phases[channel];
    if(reader_valid) return; /* Preserve the valid frame as the last candidate. */
    if(p->count<9) {
        if(!bit) { p->count=0; p->ready=1; return; }
        if(!p->ready) return;
        if(++p->count==9) { p->frame[0]=0xff; p->frame[1]=0x80; }
        return;
    }
    /* Append directly to the current byte; do not shift all eight bytes
     * on every decoded bit. Every payload bit is overwritten before use. */
    j=p->count>>3;
    if(!(p->count&7)) p->frame[j]=0;
    p->frame[j]|=bit<<(7-(p->count&7));
    ++p->count;
    if(p->count==32 && !reader_prefix_ready) {
        for(j=0;j<4;++j) reader_prefix[j]=p->frame[j];
        reader_prefix_ready=1;
    }
    if(p->count==64) {
        p->count=0; p->ready=!bit; /* Stop zero can precede the next header. */
        if(reader_frame_count==4) { reader_full=1; return; }
        for(j=0;j<8;++j) reader_frames[reader_frame_count][j]=p->frame[j];
        ++reader_frame_count;
        if(frame_valid(p->frame)) reader_valid=1;
        if(reader_frame_count==4) reader_full=1;
    }
}
static void decoder_half(unsigned char level)
{
    unsigned char i;
    for(i=0;i<4;++i) {
        if((i&1)==half_parity) {
            phases[i].first=level;
            phases[i].have=1;
        } else if(phases[i].have) {
            phases[i].have=0;
            if(phases[i].first==level) phases[i].count=phases[i].ready=0;
            else decoder_bit(i,phases[i].first ^ (i>>1));
        }
    }
    half_parity ^= 1;
}
static void decoder_transition(unsigned int tick, unsigned char old_level)
{
    unsigned int delta=tick-decoder_edge;
    decoder_edge=tick;
    ++reader_edges;
    if(reader_clock_tracking==2) {
        edge_seen=1; header_transition(tick,old_level); return;
    }
    if(!edge_seen) { edge_seen=1; decoder_clock=tick; return; }
    if(reader_clock_tracking) {
        /* RF/64 phase tracker: retain the half-bit grid across jitter,
         * correcting one quarter of the phase error at each accepted edge. */
        delta=tick-decoder_clock;
        if(delta<256) return; /* Early transition; do not advance the clock. */
        if(delta>1280) {
            decoder_clock=tick; decoder_sync_reset(); ++reader_resets; return;
        }
        if(delta>=768) {
            decoder_clock+=1024+(int)(delta-1024u)/4;
            decoder_half(old_level); decoder_half(old_level);
        } else {
            decoder_clock+=512+(int)(delta-512u)/4;
            decoder_half(old_level);
        }
        return;
    }
    /* Wide initial bounds for measured asymmetric comparator pulses. */
    if(delta<240 || delta>1400) {
        decoder_sync_reset(); ++reader_resets; return;
    }
    decoder_half(old_level);
    if(delta>=768) decoder_half(old_level);
}
static void reader_reset(unsigned int tick, unsigned char level)
{
    reader_frame_count=reader_valid=reader_full=0;
    reader_prefix_ready=0;
    reader_edges=reader_resets=0;
    filter_level=filter_candidate=level;
    filter_since=decoder_edge=tick;
    edge_seen=0;
    header_reset();
    decoder_sync_reset();
}
static void reader_sample(unsigned int tick, unsigned char level)
{
    if(level!=filter_candidate) {
        filter_candidate=level; filter_since=tick;
    } else if(level!=filter_level && (unsigned int)(tick-filter_since)>=48) {
        decoder_transition(filter_since,filter_level);
        filter_level=level;
    }
    /* Reset before a full timer wrap can alias a long silence. */
    if(edge_seen && (unsigned int)(tick-decoder_edge)>2000) {
        edge_seen=0; decoder_sync_reset(); header_reset(); ++reader_resets;
    }
}
#endif
