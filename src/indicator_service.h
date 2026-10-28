#ifndef INDICATOR_SERVICE_H
#define INDICATOR_SERVICE_H
/* OPEN P2.2: high=red, low=green. Both LED drivers are active HIGH.
 * A valid local PIN submission flashes both for 150 ms, then restores OPEN.
 * Foreground timestamps use the reader's wrap-safe 0.5 us clock. */
static __xdata unsigned char indicator_open,indicator_candidate,indicator_flash;
static __xdata unsigned long indicator_changed,indicator_flash_at;

static void indicator_apply(void)
{
    P2_6=indicator_flash || !indicator_open;
    P2_7=indicator_flash || indicator_open;
}
static void indicator_reset(void)
{
    indicator_open=indicator_candidate=P2_2;
    indicator_changed=reader_clock_ticks;
    indicator_flash=0;
    indicator_apply();
}
static void indicator_submit(void)
{
    indicator_flash_at=reader_clock_ticks;
    indicator_flash=1;
    indicator_apply();
}
static void indicator_tick(void)
{
    if(P2_2!=indicator_candidate) {
        indicator_candidate=P2_2; indicator_changed=reader_clock_ticks;
    }
    if((unsigned long)(reader_clock_ticks-indicator_changed)>=40000UL)
        indicator_open=indicator_candidate;
    if(indicator_flash &&
       (unsigned long)(reader_clock_ticks-indicator_flash_at)>=300000UL)
        indicator_flash=0;
    indicator_apply();
}
static void indicator_hardware_init(void)
{
    P2M1|=0xc4; P2M0&=0x3b; /* Input/high-Z until LED latches are ready. */
    P_SW2|=0x80;
    (*(volatile __xdata unsigned char *)0xfe12)&=0x3b; /* No OPEN/LED pull-ups. */
    (*(volatile __xdata unsigned char *)0xfe32)|=0x04;
    P_SW2&=0x7f;
    indicator_reset();
    P2M0|=0xc0; P2M1&=0x3f; /* Only LEDs become push-pull outputs. */
}
#endif
