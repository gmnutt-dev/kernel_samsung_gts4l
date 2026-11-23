// SPDX-License-Identifier: GPL-2.0
/*
 * Samsung Galaxy Tab S4 Stage 6A: observational reclaim counters.
 * No allocations in reclaim, no printk/trace_printf, no policy writes.
 * /proc/tab_s4_stage6a is read-only and lifetime cumulative.
 */
#include <linux/atomic.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/ktime.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/tab_s4_stage6a.h>

#define STAGE6A_10MS  (10ULL * 1000 * 1000)
#define STAGE6A_50MS  (50ULL * 1000 * 1000)
#define STAGE6A_200MS (200ULL * 1000 * 1000)

struct stage6a_stats {
        atomic64_t calls;
        atomic64_t total_ns;
        atomic64_t max_ns;
        atomic64_t over_10ms;
        atomic64_t over_50ms;
        atomic64_t over_200ms;
};

static struct stage6a_stats direct_stats;
static struct stage6a_stats kswapd_stats;
static atomic64_t direct_reclaimed;
static atomic64_t direct_no_progress;
static atomic64_t direct_throttled;

u64 tab_s4_stage6a_start(void)
{
        return ktime_get_ns();
}

static void stage6a_record(struct stage6a_stats *stats, u64 start_ns)
{
        u64 elapsed = ktime_get_ns() - start_ns;
        u64 observed;

        atomic64_inc(&stats->calls);
        atomic64_add(elapsed, &stats->total_ns);
        observed = atomic64_read(&stats->max_ns);
        while (elapsed > observed) {
                u64 prior = atomic64_cmpxchg(&stats->max_ns,
                                             observed, elapsed);
                if (prior == observed)
                        break;
                observed = prior;
        }
        if (elapsed >= STAGE6A_10MS)
                atomic64_inc(&stats->over_10ms);
        if (elapsed >= STAGE6A_50MS)
                atomic64_inc(&stats->over_50ms);
        if (elapsed >= STAGE6A_200MS)
                atomic64_inc(&stats->over_200ms);
}

void tab_s4_stage6a_direct_end(u64 start_ns, unsigned long reclaimed,
                              bool throttled)
{
        stage6a_record(&direct_stats, start_ns);
        atomic64_add(reclaimed, &direct_reclaimed);
        if (!reclaimed && !throttled)
                atomic64_inc(&direct_no_progress);
        if (throttled)
                atomic64_inc(&direct_throttled);
}

void tab_s4_stage6a_kswapd_end(u64 start_ns)
{
        stage6a_record(&kswapd_stats, start_ns);
}

#define SHOW_METRIC(m, key, stat) \
        seq_printf((m), key " %llu\n", \
                   (unsigned long long)atomic64_read(&(stat)))

static int stage6a_show(struct seq_file *m, void *v)
{
        seq_puts(m, "version 1\n");
        seq_puts(m, "scope lifetime_cumulative\n");
        seq_puts(m, "duration_unit ns\n");
        seq_puts(m, "direct_duration_scope includes_throttle\n");
        seq_puts(m, "kswapd_duration_scope balance_pgdat\n");
        SHOW_METRIC(m, "direct_calls", direct_stats.calls);
        SHOW_METRIC(m, "direct_total_ns", direct_stats.total_ns);
        SHOW_METRIC(m, "direct_max_ns", direct_stats.max_ns);
        SHOW_METRIC(m, "direct_over_10ms", direct_stats.over_10ms);
        SHOW_METRIC(m, "direct_over_50ms", direct_stats.over_50ms);
        SHOW_METRIC(m, "direct_over_200ms", direct_stats.over_200ms);
        SHOW_METRIC(m, "direct_pages_reclaimed", direct_reclaimed);
        SHOW_METRIC(m, "direct_no_progress", direct_no_progress);
        SHOW_METRIC(m, "direct_throttled", direct_throttled);
        SHOW_METRIC(m, "kswapd_calls", kswapd_stats.calls);
        SHOW_METRIC(m, "kswapd_total_ns", kswapd_stats.total_ns);
        SHOW_METRIC(m, "kswapd_max_ns", kswapd_stats.max_ns);
        SHOW_METRIC(m, "kswapd_over_10ms", kswapd_stats.over_10ms);
        SHOW_METRIC(m, "kswapd_over_50ms", kswapd_stats.over_50ms);
        SHOW_METRIC(m, "kswapd_over_200ms", kswapd_stats.over_200ms);
        return 0;
}

static int stage6a_open(struct inode *inode, struct file *file)
{
        return single_open(file, stage6a_show, NULL);
}

static const struct file_operations stage6a_fops = {
        .owner = THIS_MODULE,
        .open = stage6a_open,
        .read = seq_read,
        .llseek = seq_lseek,
        .release = single_release,
};

static int __init stage6a_init(void)
{
        if (!proc_create("tab_s4_stage6a", 0444, NULL, &stage6a_fops))
                return -ENOMEM;
        return 0;
}
fs_initcall(stage6a_init);
