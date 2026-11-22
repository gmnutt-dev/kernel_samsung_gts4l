#!/bin/sh
# Source and config smoke check; not a native compile.
set -eu
cd "$(dirname "$0")/.."
test -f mm/vmscan.c
cfg=arch/arm64/configs/gts4lwifi_defconfig
for flag in TAB_S4_V59_WORKINGSET TAB_S4_V59_RECLAIM_COST \
    TAB_S4_V73_REFAULT_AGE TAB_S4_V73_COST_SIGNALS \
    TAB_S4_V73_FILE_CACHE_GUARD TAB_S4_V73_RECLAIM_YIELD; do
    grep -qx "CONFIG_${flag}=y" "$cfg"
    grep -q "CONFIG_${flag}" mm/Kconfig || grep -q "config ${flag}" mm/Kconfig
done
grep -qx '# CONFIG_ZSWAP is not set' "$cfg"
grep -qx '# CONFIG_DIRECT_RECLAIM_FILE_PAGES_ONLY is not set' "$cfg"
! grep -q 'CONFIG_TAB_S4_V59_CACHE_TRIM' mm/vmscan.c
! grep -q 'CONFIG_TAB_S4_V59_REFAULT_DEACTIVATE' mm/vmscan.c
grep -q 'CONFIG_TAB_S4_V73_COST_SIGNALS' mm/vmscan.c
grep -q 'CONFIG_TAB_S4_V73_COST_SIGNALS' mm/swap.c
grep -q 'CONFIG_TAB_S4_V73_REFAULT_AGE' mm/workingset.c
grep -q 'CONFIG_TAB_S4_V73_FILE_CACHE_GUARD' mm/vmscan.c
grep -q 'CONFIG_TAB_S4_V73_RECLAIM_YIELD' mm/vmscan.c
echo "PASS: source gating, original ZSWAP exclusion, no Stage 3 policies"
