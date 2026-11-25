/* Stage 6I: per-CPU cumulative elapsed-time histograms; no allocation
 * or logging in the measured paths. Reads are best-effort snapshots.
 */
#include <linux/atomic.h>
#include <linux/cpu.h>
#include <linux/export.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/percpu.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/tab_s4_stage6i.h>

bool tab_s4_latency_on;
module_param_named(enabled, tab_s4_latency_on, bool, 0644);
EXPORT_SYMBOL_GPL(tab_s4_latency_on);

static const u64 limits[] = {
	10000, 100000, 1000000, 5000000, 10000000, 50000000,
	100000000, 250000000, 500000000, 1000000000,
	2000000000ULL, 5000000000ULL
};
#define BUCKETS (ARRAY_SIZE(limits) + 1)
struct tab_latency {
	atomic64_t count, sum, max, bucket[BUCKETS];
};
struct tab_latency_cpu {
	struct tab_latency metric[TAB_LAT_COUNT];
};
static DEFINE_PER_CPU(struct tab_latency_cpu, tab_latency);
static const char * const names[] = {
	"direct", "kswapd", "stage6c", "swap_fault",
	"zram_read", "decompress", "runqueue_ge1ms"
};

void tab_s4_latency_record(enum tab_s4_latency_kind kind, u64 ns)
{
	struct tab_latency *p;
	s64 old, prev;
	unsigned int i = 0;
	int cpu;

	if (kind >= TAB_LAT_COUNT || !tab_s4_latency_enabled())
		return;
	while (i < ARRAY_SIZE(limits) && ns >= limits[i])
		i++;
	cpu = get_cpu();
	p = &per_cpu(tab_latency, cpu).metric[kind];
	atomic64_inc(&p->bucket[i]);
	atomic64_inc(&p->count);
	atomic64_add(ns, &p->sum);
	old = atomic64_read(&p->max);
	while (ns > (u64)old) {
		prev = atomic64_cmpxchg(&p->max, old, ns);
		if (prev == old)
			break;
		old = prev;
	}
	put_cpu();
}
EXPORT_SYMBOL_GPL(tab_s4_latency_record);

static int latency_show(struct seq_file *m, void *unused)
{
	unsigned int kind, i;
	int cpu;
	u64 count, sum, max_ns, buckets[BUCKETS];

	seq_printf(m, "version 1\nenabled %u\n", tab_s4_latency_enabled());
	seq_printf(m, "snapshot_monotonic_ns %llu\n", (unsigned long long)ktime_get_ns());
	for (kind = 0; kind < TAB_LAT_COUNT; kind++) {
		count = sum = max_ns = 0;
		memset(buckets, 0, sizeof(buckets));
		for_each_possible_cpu(cpu) {
			struct tab_latency *p = &per_cpu(tab_latency, cpu).metric[kind];
			count += atomic64_read(&p->count);
			sum += atomic64_read(&p->sum);
			max_ns = max_t(u64, max_ns, atomic64_read(&p->max));
			for (i = 0; i < BUCKETS; i++)
				buckets[i] += atomic64_read(&p->bucket[i]);
		}
		seq_printf(m, "%s_count %llu\n%s_sum_ns %llu\n%s_max_ns %llu\n",
			names[kind], (unsigned long long)count,
			names[kind], (unsigned long long)sum,
			names[kind], (unsigned long long)max_ns);
		for (i = 0; i < BUCKETS; i++) {
			if (i < ARRAY_SIZE(limits))
				seq_printf(m, "%s_lt_%llu_ns %llu\n", names[kind],
					(unsigned long long)limits[i],
					(unsigned long long)buckets[i]);
			else
				seq_printf(m, "%s_ge_5000000000_ns %llu\n",
					names[kind], (unsigned long long)buckets[i]);
		}
	}
	return 0;
}
static int latency_open(struct inode *inode, struct file *file)
{
	return single_open(file, latency_show, NULL);
}
static const struct file_operations latency_fops = {
	.owner = THIS_MODULE, .open = latency_open, .read = seq_read,
	.llseek = seq_lseek, .release = single_release,
};
static int __init latency_init(void)
{
	return proc_create("tab_s4_stage6i", 0444, NULL, &latency_fops) ? 0 : -ENOMEM;
}
late_initcall(latency_init);
