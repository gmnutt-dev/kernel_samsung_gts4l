/* SPDX-License-Identifier: GPL-2.0 */
/* Stage 6C: event-directed and strictly bounded background reclaim. */
#ifndef _LINUX_TAB_S4_STAGE6C_H
#define _LINUX_TAB_S4_STAGE6C_H

#include <linux/types.h>

#ifdef CONFIG_TAB_S4_STAGE6C
extern int tab_s4_stage6c_enable;
extern int tab_s4_stage6c_budget;
bool tab_s4_stage6c_claim(void);
void tab_s4_stage6c_record(unsigned long requested, unsigned long reclaimed,
                           unsigned long file_requested,
                           unsigned long anon_requested);
void tab_s4_stage6c_skip(unsigned int reason);
enum {
        STAGE6C_SKIP_WATERMARK,
        STAGE6C_SKIP_ACTIVITY,
        STAGE6C_SKIP_NO_PAGES,
        STAGE6C_SKIP_SWAP,
        STAGE6C_SKIP_BOOST,
        STAGE6C_SKIP_MAX
};
#else
static inline bool tab_s4_stage6c_claim(void) { return false; }
static inline void tab_s4_stage6c_record(unsigned long requested,
                                          unsigned long reclaimed,
                                          unsigned long file_requested,
                                          unsigned long anon_requested) { }
static inline void tab_s4_stage6c_skip(unsigned int reason) { }
#endif

#endif
