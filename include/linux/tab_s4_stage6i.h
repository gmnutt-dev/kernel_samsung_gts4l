/* Stage 6I observational latency counters. */
#ifndef _LINUX_TAB_S4_STAGE6I_H
#define _LINUX_TAB_S4_STAGE6I_H
#include <linux/types.h>
#include <linux/ktime.h>
#include <linux/compiler.h>

enum tab_s4_latency_kind {
	TAB_LAT_DIRECT, TAB_LAT_KSWAPD, TAB_LAT_6C, TAB_LAT_SWAP_FAULT,
	TAB_LAT_ZRAM_READ, TAB_LAT_DECOMPRESS, TAB_LAT_RUNQUEUE, TAB_LAT_COUNT
};

#ifdef CONFIG_TAB_S4_STAGE6I
extern bool tab_s4_latency_on;
void tab_s4_latency_record(enum tab_s4_latency_kind kind, u64 ns);
static inline bool tab_s4_latency_enabled(void)
{
	return unlikely(READ_ONCE(tab_s4_latency_on));
}
#else
static inline bool tab_s4_latency_enabled(void) { return false; }
static inline void tab_s4_latency_record(enum tab_s4_latency_kind kind, u64 ns) {}
#endif

static inline u64 tab_s4_latency_start(void)
{
	return tab_s4_latency_enabled() ? ktime_get_ns() : 0;
}
static inline u64 tab_s4_latency_elapsed(u64 start)
{
	return start ? ktime_get_ns() - start : 0;
}
static inline void tab_s4_latency_end(enum tab_s4_latency_kind kind, u64 start)
{
	if (start)
		tab_s4_latency_record(kind, tab_s4_latency_elapsed(start));
}
#endif
