// SPDX-License-Identifier: GPL-2.0
/*
 * Tab S4 Stage 6D: derived context from live memory signals.
 *
 * Same LRU vectors as 6B, but independent bounded history so mode 0
 * preserves 6B scan decisions. No per-page metadata, address ownership
 * tables, per-access tracing, timers, allocations or extra workers.
 *
 * The value is a scanning PREFERENCE, not a refcount or page pin.
 * Every score update and read is serialized by the existing zone lru_lock.
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
#include <linux/swap.h>
#include <linux/tab_s4_stage6d.h>

#define STAGE6D_SCORE_MAX       15U
#define STAGE6D_QUALITY_MAX      8U
#define STAGE6D_REF_CREDIT_INTERVAL max_t(unsigned long, 1UL, HZ / 4)
#define STAGE6D_RECENT_WINDOW   (2UL * HZ)
#define STAGE6D_MIN_SAMPLE      16UL

/* Zero = observation-only, retaining all existing 6B/6C decisions. */
int tab_s4_stage6d_mode;
/* Deliberately below 6B's 25% cap by default; 0..25 via sysctl. */
int tab_s4_stage6d_cap = 20;

static atomic64_t refault_events[2];
static atomic64_t refault_credits[2];
static atomic64_t renewal_events[2];
static atomic64_t rotated_events[2];
static atomic64_t efficient_reclaim_events[2];
static atomic64_t inefficient_reclaim_events[2];
static atomic64_t protected_targets[2];
static atomic64_t deferred_pages[2];
static atomic64_t scored_reads;
static atomic64_t pressure_readouts[3]; /* relaxed / transitional / high */
static atomic64_t recent_context_biases[2];
static atomic64_t quality_demotions[2];

/*
 * "Chemical soup" analogue: memory pressure is inferred from current
 * logical swap occupancy rather than explicit application ownership.
 * The caller already holds the LRU lock on event paths, but the swap
 * headroom getter is an atomic read and does not take that LRU lock.
 */
static unsigned int stage6d_pressure(void)
{
        long free_swap;

        if (total_swap_pages <= 0)
                return 2;
        free_swap = get_nr_swap_pages();
        if (free_swap > total_swap_pages / 2)
                return 0;
        if (free_swap > total_swap_pages / 4)
                return 1;
        return 2;
}

static unsigned long stage6d_fast_period(unsigned int pressure)
{
        if (!pressure)
                return 2UL * HZ;
        if (pressure == 1)
                return HZ;
        return max_t(unsigned long, 1UL, HZ / 2);
}

static unsigned long stage6d_slow_period(unsigned int pressure)
{
        if (!pressure)
                return 16UL * HZ;
        if (pressure == 1)
                return 8UL * HZ;
        return 4UL * HZ;
}

/* Must be called under zone->lru_lock; never holds or pins any page. */
static void stage6d_age_locked(struct zone_reclaim_stat *st,
                             unsigned int type, unsigned int pressure)
{
        unsigned long now = jiffies;
        unsigned long period = stage6d_fast_period(pressure);
        unsigned long elapsed;

        if (!st->stage6d_fast_jiffies[type]) {
                st->stage6d_fast_jiffies[type] = now;
        } else if (time_after_eq(now,
                                  st->stage6d_fast_jiffies[type] + period)) {
                elapsed = (now - st->stage6d_fast_jiffies[type]) / period;
                st->stage6d_fast[type] -=
                        min_t(unsigned long, st->stage6d_fast[type], elapsed);
                st->stage6d_fast_jiffies[type] += elapsed * period;
        }

        period = stage6d_slow_period(pressure);
        if (!st->stage6d_slow_jiffies[type]) {
                st->stage6d_slow_jiffies[type] = now;
        } else if (time_after_eq(now,
                                  st->stage6d_slow_jiffies[type] + period)) {
                elapsed = (now - st->stage6d_slow_jiffies[type]) / period;
                st->stage6d_slow[type] -=
                        min_t(unsigned long, st->stage6d_slow[type], elapsed);
                /* Productive-reclaim history eventually returns to neutral. */
                st->stage6d_quality[type] -=
                        min_t(unsigned long, st->stage6d_quality[type], elapsed);
                st->stage6d_slow_jiffies[type] += elapsed * period;
        }
}

void tab_s4_stage6d_refault_locked(struct lruvec *lruvec, bool file)
{
        struct zone_reclaim_stat *st = &lruvec->reclaim_stat;
        unsigned int type = file ? 1 : 0;
        unsigned long now = jiffies;
        unsigned long previous = st->stage6d_last_refault[type];

        stage6d_age_locked(st, type, stage6d_pressure());
        /* Transition hint: a newly refaulting class after a quiet interval. */
        if (previous && time_after(now, previous + STAGE6D_RECENT_WINDOW))
                atomic64_inc(&renewal_events[type]);
        st->stage6d_last_refault[type] = now;

        atomic64_inc(&refault_events[type]);

        /*
         * Avoid having a burst of hundreds of refaults instantly saturate
         * both histories. Only grant one credit per quarter-second per
         * (zone, memcg, anon/file) context.
         */
        if (st->stage6d_last_credit[type] &&
            time_before(now, st->stage6d_last_credit[type] +
                         STAGE6D_REF_CREDIT_INTERVAL))
                return;

        st->stage6d_last_credit[type] = now;
        st->stage6d_fast[type] = min_t(unsigned int, STAGE6D_SCORE_MAX,
                                      st->stage6d_fast[type] + 3);
        st->stage6d_slow[type] = min_t(unsigned int, STAGE6D_SCORE_MAX,
                                      st->stage6d_slow[type] + 1);
        atomic64_inc(&refault_credits[type]);
}

void tab_s4_stage6d_rotation_locked(struct lruvec *lruvec, bool file,
                                  unsigned long scanned, unsigned long rotated)
{
        struct zone_reclaim_stat *st = &lruvec->reclaim_stat;
        unsigned int type = file ? 1 : 0;

        if (scanned < STAGE6D_MIN_SAMPLE ||
            rotated < (scanned + 1) / 2)
                return;
        stage6d_age_locked(st, type, stage6d_pressure());
        if (st->stage6d_fast[type] < STAGE6D_SCORE_MAX)
                st->stage6d_fast[type]++;
        atomic64_inc(&rotated_events[type]);
}

void tab_s4_stage6d_reclaim_locked(struct lruvec *lruvec, bool file,
                                 unsigned long scanned, unsigned long reclaimed)
{
        struct zone_reclaim_stat *st = &lruvec->reclaim_stat;
        unsigned int type = file ? 1 : 0;

        if (scanned < STAGE6D_MIN_SAMPLE)
                return;
        stage6d_age_locked(st, type, stage6d_pressure());

        if (reclaimed >= scanned - scanned / 4) {
                if (st->stage6d_fast[type])
                        st->stage6d_fast[type]--;
                st->stage6d_quality[type] = min_t(unsigned int,
                        STAGE6D_QUALITY_MAX, st->stage6d_quality[type] + 2);
                atomic64_inc(&efficient_reclaim_events[type]);
        } else if (reclaimed <= scanned / 4) {
                if (st->stage6d_quality[type])
                        st->stage6d_quality[type]--;
                atomic64_inc(&inefficient_reclaim_events[type]);
        }
}

/*
 * Re-evaluate the live context instead of storing an app address map.
 * Only get_scan_count's existing guarded normal-reclaim path applies this
 * preference. An active competitor can remove stale-class bonus.
 */
void tab_s4_stage6d_percent_locked(struct lruvec *lruvec,
                                  unsigned int percentage[2])
{
        struct zone_reclaim_stat *st = &lruvec->reclaim_stat;
        unsigned int pressure = stage6d_pressure();
        unsigned long now = jiffies;
        unsigned int type, other;
        int cap = clamp_t(int, READ_ONCE(tab_s4_stage6d_cap), 0, 25);

        atomic64_inc(&scored_reads);
        atomic64_inc(&pressure_readouts[pressure]);
        for (type = 0; type < 2; type++)
                stage6d_age_locked(st, type, pressure);

        for (type = 0; type < 2; type++) {
                int score = st->stage6d_fast[type] +
                            st->stage6d_slow[type];
                bool recent, competing_recent;
                other = 1 - type;

                recent = st->stage6d_last_refault[type] &&
                         time_before_eq(now,
                            st->stage6d_last_refault[type] + STAGE6D_RECENT_WINDOW);
                competing_recent = st->stage6d_last_refault[other] &&
                         time_before_eq(now,
                            st->stage6d_last_refault[other] + STAGE6D_RECENT_WINDOW);
                if (recent) {
                        score += 2;
                        atomic64_inc(&recent_context_biases[type]);
                } else if (competing_recent && st->stage6d_fast[other] >
                           st->stage6d_fast[type] + 3) {
                        score -= 3;
                }

                if (st->stage6d_quality[type])
                        atomic64_inc(&quality_demotions[type]);
                score -= st->stage6d_quality[type];
                percentage[type] = clamp_t(int, score, 0, cap);
        }
}

void tab_s4_stage6d_deferred(bool file, unsigned long pages)
{
        unsigned int type = file ? 1 : 0;

        if (!pages)
                return;
        atomic64_inc(&protected_targets[type]);
        atomic64_add(pages, &deferred_pages[type]);
}

#define SHOW(m, name, v) \
        seq_printf((m), name " %llu\n", \
                   (unsigned long long)atomic64_read(&(v)))

static int stage6d_show(struct seq_file *m, void *ignored)
{
        seq_puts(m, "version 1\n");
        seq_puts(m, "mode_0 shadow_stage6b_unchanged\n");
        seq_puts(m, "mode_1 context_scanning_override\n");
        seq_puts(m, "scope lruvec_anon_file\n");
        seq_puts(m, "pressure relaxed_swap_free_above_50pct\n");
        seq_puts(m, "pressure medium_swap_free_25_to_50pct\n");
        seq_puts(m, "pressure high_swap_free_below_25pct\n");
        seq_puts(m, "fast_decay_seconds_by_pressure 2_1_0.5\n");
        seq_puts(m, "slow_decay_seconds_by_pressure 16_8_4\n");
        seq_puts(m, "refault_credit_min_interval_ms 250_at_HZ1000\n");
        seq_puts(m, "context_refault_window_seconds 2\n");
        seq_printf(m, "mode %d\n", READ_ONCE(tab_s4_stage6d_mode));
        seq_printf(m, "max_scan_reduction_percent %d\n",
                   READ_ONCE(tab_s4_stage6d_cap));
        SHOW(m, "anon_refault_events", refault_events[0]);
        SHOW(m, "file_refault_events", refault_events[1]);
        SHOW(m, "anon_refault_credits", refault_credits[0]);
        SHOW(m, "file_refault_credits", refault_credits[1]);
        SHOW(m, "anon_renewal_hints", renewal_events[0]);
        SHOW(m, "file_renewal_hints", renewal_events[1]);
        SHOW(m, "anon_rotation_events", rotated_events[0]);
        SHOW(m, "file_rotation_events", rotated_events[1]);
        SHOW(m, "anon_efficient_reclaim_events", efficient_reclaim_events[0]);
        SHOW(m, "file_efficient_reclaim_events", efficient_reclaim_events[1]);
        SHOW(m, "anon_inefficient_reclaim_events", inefficient_reclaim_events[0]);
        SHOW(m, "file_inefficient_reclaim_events", inefficient_reclaim_events[1]);
        SHOW(m, "scored_reads", scored_reads);
        SHOW(m, "decisions_relaxed", pressure_readouts[0]);
        SHOW(m, "decisions_medium", pressure_readouts[1]);
        SHOW(m, "decisions_high", pressure_readouts[2]);
        SHOW(m, "anon_recent_context_biases", recent_context_biases[0]);
        SHOW(m, "file_recent_context_biases", recent_context_biases[1]);
        SHOW(m, "anon_quality_demotions", quality_demotions[0]);
        SHOW(m, "file_quality_demotions", quality_demotions[1]);
        SHOW(m, "anon_protected_scan_targets", protected_targets[0]);
        SHOW(m, "file_protected_scan_targets", protected_targets[1]);
        SHOW(m, "anon_scan_target_pages_deferred", deferred_pages[0]);
        SHOW(m, "file_scan_target_pages_deferred", deferred_pages[1]);
        return 0;
}

static int stage6d_open(struct inode *inode, struct file *file)
{
        return single_open(file, stage6d_show, NULL);
}

static const struct file_operations stage6d_fops = {
        .owner = THIS_MODULE,
        .open = stage6d_open,
        .read = seq_read,
        .llseek = seq_lseek,
        .release = single_release,
};

static int __init stage6d_init(void)
{
        if (!proc_create("tab_s4_stage6d", 0444, NULL, &stage6d_fops))
                return -ENOMEM;
        return 0;
}
late_initcall(stage6d_init);
