/* SPDX-License-Identifier: GPL-2.0 */
/* Tab S4 Stage 6A observation hooks. No reclaim-policy changes. */
#ifndef _LINUX_TAB_S4_STAGE6A_H
#define _LINUX_TAB_S4_STAGE6A_H

#include <linux/types.h>

#ifdef CONFIG_TAB_S4_STAGE6A
u64 tab_s4_stage6a_start(void);
void tab_s4_stage6a_direct_end(u64 start_ns, unsigned long reclaimed,
                              bool throttled);
void tab_s4_stage6a_kswapd_end(u64 start_ns);
#else
static inline u64 tab_s4_stage6a_start(void) { return 0; }
static inline void tab_s4_stage6a_direct_end(u64 start_ns,
                   unsigned long reclaimed, bool throttled) { }
static inline void tab_s4_stage6a_kswapd_end(u64 start_ns) { }
#endif

#endif /* _LINUX_TAB_S4_STAGE6A_H */
