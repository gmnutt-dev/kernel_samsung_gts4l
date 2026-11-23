#!/usr/bin/env bash
# Stage 6C additive source-safety checks; not a replacement for compilation.
set -euo pipefail
root="${1:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
fail(){ printf 'FAIL: %s\n' "$*" >&2; exit 1; }
has(){ grep -Fq -- "$2" "$root/$1" || fail "$1 missing [$2]"; }

has mm/Kconfig 'config TAB_S4_STAGE6C'
has mm/Kconfig 'depends on TAB_S4_STAGE6B && PROC_FS && SYSCTL'
has mm/Makefile 'obj-$(CONFIG_TAB_S4_STAGE6C) += tab_s4_stage6c.o'
has arch/arm64/configs/gts4lwifi_defconfig 'CONFIG_TAB_S4_STAGE6C=y'
has arch/arm64/configs/gts4lwifi_defconfig 'CONFIG_TAB_S4_STAGE6B=y'
has arch/arm64/configs/gts4lwifi_defconfig 'CONFIG_TAB_S4_LMKD_VMSTAT_COMPAT=y'
has arch/arm64/configs/gts4lwifi_defconfig '# CONFIG_ZSWAP is not set'
has arch/arm64/configs/gts4lwifi_defconfig '# CONFIG_DIRECT_RECLAIM_FILE_PAGES_ONLY is not set'
has mm/tab_s4_stage6c.c 'int tab_s4_stage6c_enable;'
has mm/tab_s4_stage6c.c 'int tab_s4_stage6c_budget = 64;'
has mm/tab_s4_stage6c.c '#define STAGE6C_PRESSURE_MIN      30UL'
has mm/tab_s4_stage6c.c '#define STAGE6C_PRESSURE_MAX      79UL'
has mm/tab_s4_stage6c.c 'STAGE6C_COOLDOWN          (3UL * HZ)'
has mm/tab_s4_stage6c.c 'STAGE6C_EVENT_TTL         (2UL * HZ)'
has mm/tab_s4_stage6c.c 'vmpressure_notifier_register(&stage6c_notifier)'
has mm/tab_s4_stage6c.c 'wake_up_interruptible(&pgdat->kswapd_wait)'
has mm/tab_s4_stage6c.c 'atomic_cmpxchg(&stage6c_pending, 0, 1)'
has mm/vmscan.c 'tab_s4_stage6c_kswapd_step(pgdat)'
has mm/vmscan.c 'high_wmark_pages(zone) + extra'
has mm/vmscan.c 'tab_s4_stage6b_percent_locked(lruvec, activity)'
has mm/vmscan.c 'budget - requested, SWAP_CLUSTER_MAX'
has mm/vmscan.c 'mem_cgroup_iter_break(NULL, memcg)'
has mm/vmscan.c 'shrink_inactive_list(batch, lruvec, &sc, lru)'
has mm/vmscan.c 'get_nr_swap_pages() <= total_swap_pages / 2'
has kernel/sysctl.c '"tab_s4_stage6c_enable"'
has kernel/sysctl.c '"tab_s4_stage6c_budget"'
has mm/tab_s4_stage6c.c 'proc_create("tab_s4_stage6c", 0444'
if grep -Eq '\b(kthread_run|kthread_create|queue_delayed_work|mod_timer|SetPageUnevictable)\s*\(' "$root/mm/tab_s4_stage6c.c"; then
  fail 'Stage 6C attempts to start a worker/timer or pin pages'
fi
printf 'PASS Stage 6C static invariants; native ARM64 build and device testing remain mandatory\n'
