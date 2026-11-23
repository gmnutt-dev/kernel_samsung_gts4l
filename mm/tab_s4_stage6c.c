// SPDX-License-Identifier: GPL-2.0
/*
 * Tab S4 Stage 6C, driven by existing global vmpressure notifications.
 * No timers, no new worker or thread. Wake the existing kswapd only.
 * Mode 0 (default) means exact Stage 6B reclaim policy and no extra scans.
 */
#include <linux/atomic.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/mmzone.h>
#include <linux/module.h>
#include <linux/notifier.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/swap.h>
#include <linux/tab_s4_stage6c.h>
#include <linux/vmpressure.h>

/* Exposed as /proc/sys/vm/tab_s4_stage6c_{enable,budget}. */
int tab_s4_stage6c_enable;
int tab_s4_stage6c_budget = 64;

/* Only moderate, actionable pressure. Severe pressure belongs to LMKD/OOM. */
#define STAGE6C_PRESSURE_MIN      30UL
#define STAGE6C_PRESSURE_MAX      79UL
#define STAGE6C_COOLDOWN          (3UL * HZ)
#define STAGE6C_EVENT_TTL         (2UL * HZ)

static atomic_t stage6c_pending = ATOMIC_INIT(0);
static unsigned long stage6c_last_event;
static atomic64_t stage6c_events_seen = ATOMIC64_INIT(0);
static atomic64_t stage6c_events_accepted = ATOMIC64_INIT(0);
static atomic64_t stage6c_events_coalesced = ATOMIC64_INIT(0);
static atomic64_t stage6c_events_low = ATOMIC64_INIT(0);
static atomic64_t stage6c_events_high = ATOMIC64_INIT(0);
static atomic64_t stage6c_events_swap = ATOMIC64_INIT(0);
static atomic64_t stage6c_events_cooldown = ATOMIC64_INIT(0);
static atomic64_t stage6c_events_expired = ATOMIC64_INIT(0);
static atomic64_t stage6c_runs = ATOMIC64_INIT(0);
static atomic64_t stage6c_requested_pages = ATOMIC64_INIT(0);
static atomic64_t stage6c_reclaimed_pages = ATOMIC64_INIT(0);
static atomic64_t stage6c_file_requested = ATOMIC64_INIT(0);
static atomic64_t stage6c_anon_requested = ATOMIC64_INIT(0);
static atomic64_t stage6c_skips[STAGE6C_SKIP_MAX];

static int stage6c_pressure_event(struct notifier_block *nb,
                                   unsigned long pressure, void *unused)
{
        int nid;

        atomic64_inc(&stage6c_events_seen);
        if (!READ_ONCE(tab_s4_stage6c_enable))
                return NOTIFY_DONE;
        if (pressure < STAGE6C_PRESSURE_MIN) {
                atomic64_inc(&stage6c_events_low);
                return NOTIFY_DONE;
        }
        if (pressure > STAGE6C_PRESSURE_MAX) {
                atomic64_inc(&stage6c_events_high);
                return NOTIFY_DONE;
        }
        if (total_swap_pages <= 0 ||
            get_nr_swap_pages() <= total_swap_pages / 2) {
                atomic64_inc(&stage6c_events_swap);
                return NOTIFY_DONE;
        }

        /* One outstanding request; a new event never allocates a new job. */
        if (atomic_cmpxchg(&stage6c_pending, 0, 1)) {
                atomic64_inc(&stage6c_events_coalesced);
                return NOTIFY_DONE;
        }

        if (time_before(jiffies, READ_ONCE(stage6c_last_event) +
                         STAGE6C_COOLDOWN)) {
                atomic_set(&stage6c_pending, 0);
                atomic64_inc(&stage6c_events_cooldown);
                return NOTIFY_DONE;
        }

        WRITE_ONCE(stage6c_last_event, jiffies);
        atomic64_inc(&stage6c_events_accepted);

        /* Existing kswapd: do not alter its normal watermarks or priorities. */
        for_each_node_state(nid, N_MEMORY) {
                pg_data_t *pgdat = NODE_DATA(nid);

                if (pgdat->kswapd)
                        wake_up_interruptible(&pgdat->kswapd_wait);
        }

        return NOTIFY_OK;
}

bool tab_s4_stage6c_claim(void)
{
        unsigned long event_jiffies;

        if (!atomic_xchg(&stage6c_pending, 0))
                return false;
        if (!READ_ONCE(tab_s4_stage6c_enable))
                return false;
        event_jiffies = READ_ONCE(stage6c_last_event);
        if (time_after(jiffies, event_jiffies + STAGE6C_EVENT_TTL)) {
                atomic64_inc(&stage6c_events_expired);
                return false;
        }
        atomic64_inc(&stage6c_runs);
        return true;
}

void tab_s4_stage6c_record(unsigned long requested, unsigned long reclaimed,
                           unsigned long file_requested,
                           unsigned long anon_requested)
{
        atomic64_add(requested, &stage6c_requested_pages);
        atomic64_add(reclaimed, &stage6c_reclaimed_pages);
        atomic64_add(file_requested, &stage6c_file_requested);
        atomic64_add(anon_requested, &stage6c_anon_requested);
}

void tab_s4_stage6c_skip(unsigned int reason)
{
        if (reason < STAGE6C_SKIP_MAX)
                atomic64_inc(&stage6c_skips[reason]);
}

#define SHOW(m, key, c) \
        seq_printf(m, key " %llu\n", \
                   (unsigned long long)atomic64_read(&(c)))

static int stage6c_show(struct seq_file *m, void *ignored)
{
        seq_puts(m, "version 1\n");
        seq_puts(m, "scope lifetime_cumulative\n");
        seq_puts(m, "event_source global_vmpressure\n");
        seq_puts(m, "proactive_thread existing_kswapd\n");
        seq_puts(m, "pressure_min 30\npressure_max 79\n");
        seq_puts(m, "cooldown_seconds 3\nevent_ttl_seconds 2\n");
        seq_printf(m, "enabled %d\n", READ_ONCE(tab_s4_stage6c_enable));
        seq_printf(m, "budget_pages %d\n", READ_ONCE(tab_s4_stage6c_budget));
        seq_printf(m, "pending %d\n", atomic_read(&stage6c_pending));
        SHOW(m, "events_seen", stage6c_events_seen);
        SHOW(m, "events_accepted", stage6c_events_accepted);
        SHOW(m, "events_coalesced", stage6c_events_coalesced);
        SHOW(m, "events_below_threshold", stage6c_events_low);
        SHOW(m, "events_above_threshold", stage6c_events_high);
        SHOW(m, "events_swap_guard", stage6c_events_swap);
        SHOW(m, "events_cooldown", stage6c_events_cooldown);
        SHOW(m, "events_expired", stage6c_events_expired);
        SHOW(m, "runs", stage6c_runs);
        SHOW(m, "requested_pages", stage6c_requested_pages);
        SHOW(m, "reclaimed_pages", stage6c_reclaimed_pages);
        SHOW(m, "file_requested_pages", stage6c_file_requested);
        SHOW(m, "anon_requested_pages", stage6c_anon_requested);
        SHOW(m, "skipped_watermark", stage6c_skips[STAGE6C_SKIP_WATERMARK]);
        SHOW(m, "skipped_activity", stage6c_skips[STAGE6C_SKIP_ACTIVITY]);
        SHOW(m, "skipped_no_pages", stage6c_skips[STAGE6C_SKIP_NO_PAGES]);
        SHOW(m, "skipped_swap", stage6c_skips[STAGE6C_SKIP_SWAP]);
        SHOW(m, "skipped_boost", stage6c_skips[STAGE6C_SKIP_BOOST]);
        return 0;
}

static int stage6c_open(struct inode *inode, struct file *file)
{
        return single_open(file, stage6c_show, NULL);
}

static const struct file_operations stage6c_fops = {
        .owner = THIS_MODULE,
        .open = stage6c_open,
        .read = seq_read,
        .llseek = seq_lseek,
        .release = single_release,
};

static struct notifier_block stage6c_notifier = {
        .notifier_call = stage6c_pressure_event,
};

static int __init stage6c_init(void)
{
        int ret;

        ret = vmpressure_notifier_register(&stage6c_notifier);
        if (ret)
                return ret;
        if (!proc_create("tab_s4_stage6c", 0444, NULL, &stage6c_fops)) {
                vmpressure_notifier_unregister(&stage6c_notifier);
                return -ENOMEM;
        }
        return 0;
}
late_initcall(stage6c_init);
