/* SPDX-License-Identifier: GPL-2.0 */
/* Stage 6D: pressure-conditioned context history. No per-page metadata. */
#ifndef _LINUX_TAB_S4_STAGE6D_H
#define _LINUX_TAB_S4_STAGE6D_H

#include <linux/types.h>

struct lruvec;

#ifdef CONFIG_TAB_S4_STAGE6D
/* 0 = observe, 1 = use Stage 6D instead of Stage 6B scan scores. */
extern int tab_s4_stage6d_mode;
extern int tab_s4_stage6d_cap;

/* All _locked functions require the owning zone->lru_lock. */
void tab_s4_stage6d_refault_locked(struct lruvec *lruvec, bool file);
void tab_s4_stage6d_rotation_locked(struct lruvec *lruvec, bool file,
                                  unsigned long scanned, unsigned long rotated);
void tab_s4_stage6d_reclaim_locked(struct lruvec *lruvec, bool file,
                                 unsigned long scanned, unsigned long reclaimed);
void tab_s4_stage6d_percent_locked(struct lruvec *lruvec,
                                  unsigned int percentage[2]);
void tab_s4_stage6d_deferred(bool file, unsigned long pages);
#endif

#endif /* _LINUX_TAB_S4_STAGE6D_H */
