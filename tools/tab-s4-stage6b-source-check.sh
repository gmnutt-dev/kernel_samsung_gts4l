#!/usr/bin/env bash
# Stage 6B source invariant checks. No claim of an ARM64 native build.
set -euo pipefail
src="${1:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}"
fail(){ printf 'FAIL: %s\n' "$*" >&2; exit 1; }
has(){ grep -Fq -- "$2" "$src/$1" || fail "$1 missing [$2]"; }

has mm/Kconfig 'config TAB_S4_STAGE6B'
has mm/Kconfig 'depends on TAB_S4_V59_ANON_WORKINGSET && TAB_S4_V59_RECLAIM_RETRY'
has mm/Makefile 'obj-$(CONFIG_TAB_S4_STAGE6B) += tab_s4_stage6b.o'
has arch/arm64/configs/gts4lwifi_defconfig 'CONFIG_TAB_S4_STAGE6B=y'
has arch/arm64/configs/gts4lwifi_defconfig 'CONFIG_TAB_S4_LMKD_VMSTAT_COMPAT=y'
has arch/arm64/configs/gts4lwifi_defconfig '# CONFIG_ZSWAP is not set'
has arch/arm64/configs/gts4lwifi_defconfig '# CONFIG_DIRECT_RECLAIM_FILE_PAGES_ONLY is not set'
for marker in 'stage6b_fast[2]' 'stage6b_slow[2]' 'stage6b_fast_jiffies[2]' 'stage6b_slow_jiffies[2]'; do
  has include/linux/mmzone.h "$marker"
done
has mm/swap.c 'tab_s4_stage6b_refault_locked(lruvec, true);'
has mm/swap.c 'tab_s4_stage6b_refault_locked(lruvec, false);'
has mm/vmscan.c 'tab_s4_stage6b_rotation_locked(lruvec, file, nr_taken, nr_rotated);'
has mm/vmscan.c 'tab_s4_stage6b_reclaim_locked(lruvec, file, nr_taken, nr_reclaimed);'
has mm/vmscan.c 'tab_s4_stage6b_percent_locked(lruvec, stage6b_protection);'
has mm/vmscan.c 'sc->priority > 3 && !sc->force_deactivate'
has mm/vmscan.c 'total_swap_pages / 16'
has mm/vmscan.c 'if (!pass && scan && stage6b_protection[file])'
has kernel/sysctl.c '.procname' # validate real text below
has kernel/sysctl.c '"tab_s4_stage6b_protection"'
has mm/tab_s4_stage6b.c 'STAGE6B_SCORE_MAX       15U'
has mm/tab_s4_stage6b.c 'STAGE6B_MAX_PROTECTION  25U'
has mm/tab_s4_stage6b.c 'STAGE6B_SLOW_PERIOD     (8UL * HZ)'
has mm/tab_s4_stage6b.c 'proc_create("tab_s4_stage6b", 0444'
if grep -Eq '\b(SetPageUnevictable|mlock|kmalloc|kzalloc|vmalloc|schedule_timeout|mod_timer)\s*\(' "$src/mm/tab_s4_stage6b.c"; then
  fail 'Stage 6B policy contains forbidden page-pinning, allocation, or timer path'
fi
printf 'PASS: Stage 6B source references, bounded scores, safety gates, and Stage 5 LMKD baseline\n'
printf 'NOTE: this does not replace a native cross-compile, device boot or behavior regression test\n'
