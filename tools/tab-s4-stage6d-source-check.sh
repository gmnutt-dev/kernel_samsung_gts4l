#!/usr/bin/env bash
# Stage 6D structural safety checks; not a native kernel build.
set -euo pipefail
src="${1:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
fail(){ printf 'FAIL: %s\n' "$*" >&2; exit 1; }
has(){ grep -Fq -- "$2" "$src/$1" || fail "$1 missing [$2]"; }

has mm/Kconfig 'config TAB_S4_STAGE6D'
has mm/Kconfig 'depends on TAB_S4_STAGE6B && TAB_S4_STAGE6C && PROC_FS && SYSCTL'
has mm/Makefile 'obj-$(CONFIG_TAB_S4_STAGE6D) += tab_s4_stage6d.o'
has arch/arm64/configs/gts4lwifi_defconfig 'CONFIG_TAB_S4_STAGE6B=y'
has arch/arm64/configs/gts4lwifi_defconfig 'CONFIG_TAB_S4_STAGE6C=y'
has arch/arm64/configs/gts4lwifi_defconfig 'CONFIG_TAB_S4_STAGE6D=y'
has arch/arm64/configs/gts4lwifi_defconfig 'CONFIG_TAB_S4_LMKD_VMSTAT_COMPAT=y'
has arch/arm64/configs/gts4lwifi_defconfig '# CONFIG_ZSWAP is not set'
has arch/arm64/configs/gts4lwifi_defconfig '# CONFIG_DIRECT_RECLAIM_FILE_PAGES_ONLY is not set'
for x in stage6d_fast stage6d_slow stage6d_quality stage6d_fast_jiffies stage6d_slow_jiffies stage6d_last_credit stage6d_last_refault; do
    has include/linux/mmzone.h "$x"
done
has mm/tab_s4_stage6b.c 'tab_s4_stage6d_refault_locked(lruvec, file)'
has mm/tab_s4_stage6b.c 'tab_s4_stage6d_rotation_locked(lruvec, file, scanned, rotated)'
has mm/tab_s4_stage6b.c 'tab_s4_stage6d_reclaim_locked(lruvec, file, scanned, reclaimed)'
has mm/vmscan.c 'tab_s4_stage6d_percent_locked(lruvec, stage6b_protection)'
has mm/vmscan.c 'tab_s4_stage6d_percent_locked(lruvec, activity)'
has mm/vmscan.c 'tab_s4_stage6d_deferred(file, deferred)'
has mm/vmscan.c 'sc->priority > 3 && !sc->force_deactivate'
has mm/vmscan.c 'total_swap_pages / 16'
has mm/tab_s4_stage6d.c 'int tab_s4_stage6d_mode;'
has kernel/sysctl.c 'stage6d_max_mode = 2;'
has mm/vmscan.c 'tab_s4_stage6d_mode) == 2'
has mm/tab_s4_stage6b.c 'if (READ_ONCE(tab_s4_stage6d_mode))'
has mm/tab_s4_stage6d.c 'int tab_s4_stage6d_cap = 20;'
has mm/tab_s4_stage6d.c 'STAGE6D_REF_CREDIT_INTERVAL'
has mm/tab_s4_stage6d.c 'stage6d_pressure(void)'
has mm/tab_s4_stage6d.c 'stage6d_quality[type]'
has mm/tab_s4_stage6d.c 'stage6d_last_refault'
has mm/tab_s4_stage6d.c 'atomic64_inc(&refault_credits[type])'
has mm/tab_s4_stage6d.c 'proc_create("tab_s4_stage6d", 0444'
has kernel/sysctl.c '"tab_s4_stage6d_mode"'
has kernel/sysctl.c '"tab_s4_stage6d_cap"'
if grep -Eq '\b(kmalloc|kzalloc|vmalloc|kthread_run|kthread_create|queue_delayed_work|mod_timer|SetPageUnevictable|mlock)\s*\(' "$src/mm/tab_s4_stage6d.c"; then
    fail 'Stage 6D introduces a forbidden allocation, thread, timer or page pin'
fi
echo 'PASS Stage 6D: bounded adaptive context, preserved Stage 6B/6C and Stage 5 safety gates'
echo 'NOTE source checks do not prove compilation, runtime parity, locking or device stability'
