/* SPDX-License-Identifier: GPL-2.0 */
/* Stage 6B: bounded per-lruvec activity history; caller holds zone->lru_lock. */
#ifndef _LINUX_TAB_S4_STAGE6B_H
#define _LINUX_TAB_S4_STAGE6B_H

#include <linux/types.h>

struct lruvec;

#ifdef CONFIG_TAB_S4_STAGE6B
extern int tab_s4_stage6b_protection;
void tab_s4_stage6b_refault_locked(struct lruvec *lruvec, bool file);
void tab_s4_stage6b_rotation_locked(struct lruvec *lruvec, bool file,
                                   unsigned long scanned, unsigned long rotated);
void tab_s4_stage6b_reclaim_locked(struct lruvec *lruvec, bool file,
                                  unsigned long scanned, unsigned long reclaimed);
void tab_s4_stage6b_percent_locked(struct lruvec *lruvec,
                                   unsigned int percentage[2]);
void tab_s4_stage6b_deferred(bool file, unsigned long pages);
#endif

#endif /* _LINUX_TAB_S4_STAGE6B_H */
