#ifndef RFID_CAPTURE_H
#define RFID_CAPTURE_H

/* Sampled edges, not hardware input capture. Timestamps are 0.5 us ticks.
 * Call at least once per Timer3 wrap (32.768 ms at 24 MHz / 12).
 */
#define RFID_EDGE_CAPACITY 192
static __xdata unsigned long edge_time[RFID_EDGE_CAPACITY];
static __xdata unsigned char edge_level[RFID_EDGE_CAPACITY];
static __xdata unsigned char edge_count, initial_level, last_level;
/* Diagnostic-only counters live in XRAM, leaving direct RAM for live ISR state. */
static __xdata unsigned int previous_tick, max_gap, dropped;
static __xdata unsigned long elapsed_ticks;

static void capture_reset(unsigned int tick, unsigned char level)
{
    previous_tick = tick;
    initial_level = last_level = level;
    edge_count = 0;
    max_gap = dropped = 0;
    elapsed_ticks = 0;
}

static void capture_sample(unsigned int tick, unsigned char level)
{
    unsigned int delta = tick - previous_tick;
    previous_tick = tick;
    elapsed_ticks += delta;
    if (delta > max_gap) max_gap = delta;
    if (level == last_level) return;
    last_level = level;
    if (edge_count < RFID_EDGE_CAPACITY) {
        edge_time[edge_count] = elapsed_ticks;
        edge_level[edge_count++] = level;
    } else if (dropped != 0xffff) ++dropped;
}
#endif
