#ifndef RFID_PRESENCE_H
#define RFID_PRESENCE_H
/* Acquisition requires parity-valid frames. Timer units 0.5 us.
 * Canonical EM4100 frames uniquely encode the five-byte ID, so comparing the
 * full valid frame is equivalent to comparing payloads. A matching 32-bit
 * prefix can extend existing presence, but never acquire or notify a tag.
 */
static __xdata unsigned char presence_frame[8];
static unsigned char presence_count;
static __xdata unsigned long presence_last_seen;
static void presence_reset(void)
{
    presence_count=0;
}
static unsigned char presence_expire(unsigned long now)
{
    if(presence_count && (unsigned long)(now-presence_last_seen)>=3000000UL) {
        presence_reset(); return 1;
    }
    return 0;
}
static unsigned char presence_acquired(const unsigned char __xdata *frame,
                                      unsigned long now)
{
    unsigned char i,same=1;
    presence_expire(now);
    if(!presence_count) same=0;
    else for(i=0;i<8;++i) if(frame[i]!=presence_frame[i]) same=0;
    if(!same) {
        for(i=0;i<8;++i) presence_frame[i]=frame[i];
        presence_count=1;
    }
    presence_last_seen=now;
    /* Presence is acquired and notified together; no second flag is needed. */
    return !same;
}
static unsigned char presence_observed_prefix(const unsigned char __xdata *prefix,
                                             unsigned long now)
{
    unsigned char i;
    if(!presence_count || (unsigned long)(now-presence_last_seen)>=3000000UL) return 0;
    for(i=0;i<4;++i) if(prefix[i]!=presence_frame[i]) return 0;
    presence_last_seen=now;
    return 1;
}
#endif
