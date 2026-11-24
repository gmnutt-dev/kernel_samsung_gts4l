// SPDX-License-Identifier: GPL-2.0
/*
 * Tab S4 Stage 6B: bounded, class-level activity history.
 *
 * Scores are per (zone, memcg lruvec, anon/file), never per page.
 * All score accesses take place under the existing zone->lru_lock.
 * A read-only /proc node shows lifetime event counters; no allocations,
 * timers, tracing or logging on any page-fault/reclaim path.
 */
#include <linux/atomic.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/mmzone.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/tab_s4_stage6b.h>
#ifdef CONFIG_TAB_S4_STAGE6D
#include <linux/tab_s4_stage6d.h>
#endif

#define STAGE6B_SCORE_MAX       15U
#define STAGE6B_MAX_PROTECTION  25U
#define STAGE6B_SLOW_PERIOD     (8UL * HZ)
#define STAGE6B_MIN_SAMPLE      16UL

/* 0 = observation-only; 1 = bounded scan protection (initial default). */
int tab_s4_stage6b_protection = 1;

static atomic64_t refaults[2];
static atomic64_t active_rotation_events[2];
static atomic64_t productive_reclaim_events[2];
static atomic64_t deferred_scan_pages[2];
static atomic64_t protected_scan_targets[2];

static inline unsigned int stage6b_type(bool file)
{
        return file ? 1 : 0;
}

/* Must be called under the owning zone's lru_lock. */
static void stage6b_age_locked(struct zone_reclaim_stat *stat,
                              unsigned int type)
{
        unsigned long now = jiffies;
        unsigned long elapsed;

        if (!stat->stage6b_fast_jiffies[type]) {
                stat->stage6b_fast_jiffies[type] = now;
        } else if (time_after_eq(now, stat->stage6b_fast_jiffies[type] + HZ)) {
                elapsed = (now - stat->stage6b_fast_jiffies[type]) / HZ;
                stat->stage6b_fast[type] -=
                        min_t(unsigned long, stat->stage6b_fast[type], elapsed);
                stat->stage6b_fast_jiffies[type] += elapsed * HZ;
        }

        if (!stat->stage6b_slow_jiffies[type]) {
                stat->stage6b_slow_jiffies[type] = now;
        } else if (time_after_eq(now, stat->stage6b_slow_jiffies[type] +
                                 STAGE6B_SLOW_PERIOD)) {
                elapsed = (now - stat->stage6b_slow_jiffies[type]) /
                          STAGE6B_SLOW_PERIOD;
                stat->stage6b_slow[type] -=
                        min_t(unsigned long, stat->stage6b_slow[type], elapsed);
                stat->stage6b_slow_jiffies[type] +=
                        elapsed * STAGE6B_SLOW_PERIOD;
        }
}

void tab_s4_stage6b_refault_locked(struct lruvec *lruvec, bool file)
{
        struct zone_reclaim_stat *stat = &lruvec->reclaim_stat;
        unsigned int type = stage6b_type(file);

        stage6b_age_locked(stat, type);
        stat->stage6b_fast[type] = min_t(unsigned int, STAGE6B_SCORE_MAX,
                                        stat->stage6b_fast[type] + 3U);
        stat->stage6b_slow[type] = min_t(unsigned int, STAGE6B_SCORE_MAX,
                                        stat->stage6b_slow[type] + 1U);
        atomic64_inc(&refaults[type]);
#ifdef CONFIG_TAB_S4_STAGE6D
        tab_s4_stage6d_refault_locked(lruvec, file);
#endif
}

void tab_s4_stage6b_rotation_locked(struct lruvec *lruvec, bool file,
                                   unsigned long scanned, unsigned long rotated)
{
        struct zone_reclaim_stat *stat = &lruvec->reclaim_stat;
        unsigned int type = stage6b_type(file);

        stage6b_age_locked(stat, type);
#ifdef CONFIG_TAB_S4_STAGE6D
        tab_s4_stage6d_rotation_locked(lruvec, file, scanned, rotated);
#endif
        /* Only actual referenced active pages count as a positive signal. */
        if (scanned < STAGE6B_MIN_SAMPLE || rotated < (scanned + 1) / 2)
                return;
        if (stat->stage6b_fast[type] < STAGE6B_SCORE_MAX)
                stat->stage6b_fast[type]++;
        atomic64_inc(&active_rotation_events[type]);
}

void tab_s4_stage6b_reclaim_locked(struct lruvec *lruvec, bool file,
                                  unsigned long scanned, unsigned long reclaimed)
{
        struct zone_reclaim_stat *stat = &lruvec->reclaim_stat;
        unsigned int type = stage6b_type(file);

        stage6b_age_locked(stat, type);
#ifdef CONFIG_TAB_S4_STAGE6D
        tab_s4_stage6d_reclaim_locked(lruvec, file, scanned, reclaimed);
#endif
        /* Efficient reclaim counters repeated, stale high-activity signals. */
        if (scanned < STAGE6B_MIN_SAMPLE ||
            reclaimed < scanned - scanned / 4)
                return;
        if (stat->stage6b_fast[type])
                stat->stage6b_fast[type]--;
        atomic64_inc(&productive_reclaim_events[type]);
}

void tab_s4_stage6b_percent_locked(struct lruvec *lruvec,
                                   unsigned int percentage[2])
{
        struct zone_reclaim_stat *stat = &lruvec->reclaim_stat;
        unsigned int type;

        for (type = 0; type < 2; type++) {
                stage6b_age_locked(stat, type);
                percentage[type] = min_t(unsigned int,
                        STAGE6B_MAX_PROTECTION,
                        stat->stage6b_fast[type] + stat->stage6b_slow[type]);
        }
}

void tab_s4_stage6b_deferred(bool file, unsigned long pages)
{
        unsigned int type = stage6b_type(file);

        if (!pages)
                return;
        atomic64_inc(&protected_scan_targets[type]);
        atomic64_add(pages, &deferred_scan_pages[type]);
}

#define SHOW_STAT(m, name, value) \
        seq_printf((m), name " %llu\n", \
                   (unsigned long long)atomic64_read(&(value)))

static int stage6b_show(struct seq_file *m, void *v)
{
        seq_puts(m, "version 1\n");
        seq_puts(m, "scope lifetime_cumulative\n");
        seq_puts(m, "score_scope per_lruvec_anon_file\n");
        seq_puts(m, "fast_score_max 15\n");
        seq_puts(m, "slow_score_max 15\n");
        seq_puts(m, "fast_decay_per_second 1\n");
        seq_puts(m, "slow_decay_period_seconds 8\n");
        seq_puts(m, "max_scan_reduction_percent 25\n");
        seq_printf(m, "protection_enabled %d\n",
                   READ_ONCE(tab_s4_stage6b_protection));
        SHOW_STAT(m, "anon_refault_events", refaults[0]);
        SHOW_STAT(m, "file_refault_events", refaults[1]);
        SHOW_STAT(m, "anon_rotation_events", active_rotation_events[0]);
        SHOW_STAT(m, "file_rotation_events", active_rotation_events[1]);
        SHOW_STAT(m, "anon_productive_reclaim_events",
                  productive_reclaim_events[0]);
        SHOW_STAT(m, "file_productive_reclaim_events",
                  productive_reclaim_events[1]);
        SHOW_STAT(m, "anon_protected_scan_targets", protected_scan_targets[0]);
        SHOW_STAT(m, "file_protected_scan_targets", protected_scan_targets[1]);
        SHOW_STAT(m, "anon_scan_target_pages_deferred", deferred_scan_pages[0]);
        SHOW_STAT(m, "file_scan_target_pages_deferred", deferred_scan_pages[1]);
        return 0;
}

static int stage6b_open(struct inode *inode, struct file *file)
{
        return single_open(file, stage6b_show, NULL);
}

static const struct file_operations stage6b_fops = {
        .owner = THIS_MODULE,
        .open = stage6b_open,
        .read = seq_read,
        .llseek = seq_lseek,
        .release = single_release,
};

static int __init stage6b_init(void)
{
        if (!proc_create("tab_s4_stage6b", 0444, NULL, &stage6b_fops))
                return -ENOMEM;
        return 0;
}
fs_initcall(stage6b_init);
