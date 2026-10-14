#ifndef RFID_SAMPLER_H
#define RFID_SAMPLER_H
/* Timer1 ISR producer / foreground consumer. 127 usable edges, 384 XRAM
 * bytes. A stable transition needs three samples spanning nominally 32 us.
 * Timestamps use the first observation, not qualification completion.
 * No decoder, UART or presence calls in the high-priority capture path.
 */
static volatile __xdata unsigned int sample_time[128];
static volatile __xdata unsigned char sample_old[128];
static volatile unsigned char sample_head,sample_tail,sample_fault;
static unsigned char sample_candidate,sample_level;
static unsigned char sample_gap_limit=48;
static unsigned int sample_since,sample_previous;
static volatile unsigned int sample_max_gap;
static void sampler_reset(unsigned int now,unsigned char level)
{
    sample_head=sample_tail=sample_fault=0;
    sample_candidate=sample_level=level;
    sample_since=sample_previous=now; sample_max_gap=0;
}
/* Bank1 protects registers, but not SDCC's overlaid parameter storage.
 * Keep this interrupt helper's arguments/locals out of the foreground overlay. */
#pragma save
#pragma nooverlay
static void sampler_tick(unsigned int now,unsigned char level) __using(1)
{
    unsigned int gap=now-sample_previous;
    unsigned char next;
    sample_previous=now;
    if(gap>sample_max_gap) sample_max_gap=gap;
    if(gap>sample_gap_limit) sample_fault|=1; /* >24 us means sampling timing is suspect. */
    if(sample_fault) return;
    if(level!=sample_candidate) {
        sample_candidate=level; sample_since=now;
    } else if(level!=sample_level && (unsigned int)(now-sample_since)>=48) {
        next=(sample_head+1)&127;
        if(next==sample_tail) { sample_fault|=2; return; }
        sample_time[sample_head]=sample_since;
        sample_old[sample_head]=sample_level;
        sample_level=level;
        sample_head=next;
    }
}
#pragma restore
#endif
