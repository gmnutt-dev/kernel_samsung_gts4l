#!/usr/bin/env bash
# Stage 5 source-level invariant checks, independent of cross compiler.
# Run from any directory: bash tools/tab-s4-stage5-source-check.sh [source-tree]
set -euo pipefail
src="${1:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
fail() { echo "FAIL: $*" >&2; exit 1; }
check() { local name="$1" file="$2"; grep -Fq "$name" "$src/$file" || fail "$file lacks: $name"; }

for feature in TAB_S4_V59_WORKINGSET TAB_S4_V59_RECLAIM_COST TAB_S4_V59_REFAULT_DEACTIVATE TAB_S4_V59_CACHE_TRIM TAB_S4_V59_ANON_WORKINGSET TAB_S4_V59_RECLAIM_RETRY TAB_S4_V59_WATERMARK_SCALE; do
  check "CONFIG_${feature}=y" arch/arm64/configs/gts4lwifi_defconfig
done
check '# CONFIG_DIRECT_RECLAIM_FILE_PAGES_ONLY is not set' arch/arm64/configs/gts4lwifi_defconfig
check '# CONFIG_ZSWAP is not set' arch/arm64/configs/gts4lwifi_defconfig

# Metadata must exist only with the feature and must be freed with swap slot,
# at swapoff and on failed swapon.
for marker in     'void tab_s4_swap_shadow_store(swp_entry_t entry, unsigned long shadow)'     'unsigned long tab_s4_swap_shadow_take(swp_entry_t entry)'     'p->tab_s4_anon_shadow[offset] = 0;'     'anon_shadow = vzalloc(maxpages * sizeof(*anon_shadow));'     'vfree(anon_shadow);'; do
  check "$marker" mm/swapfile.c
done

check 'tab_s4_swap_shadow_store(swap,' mm/vmscan.c
check 'workingset_eviction(mapping, page)' mm/vmscan.c
check 'shadow = tab_s4_swap_shadow_take(entry);' mm/swap_state.c
check 'workingset_refault(new_page, (void *)shadow);' mm/swap_state.c
check 'lru_cache_add(new_page);' mm/swap_state.c
check 'WORKINGSET_REFAULT_ANON' include/linux/mmzone.h
check 'WORKINGSET_REFAULT_ANON' mm/workingset.c
check 'workingset_refault_anon' mm/vmstat.c
check 'cond_resched();' mm/vmscan.c
check 'sc->force_deactivate = 1;' mm/vmscan.c
check 'get_nr_swap_pages() > SWAP_CLUSTER_MAX' mm/vmscan.c
check 'watermark_scale_factor = 50;' mm/page_alloc.c
check 'watermark_scale_factor_sysctl_handler' kernel/sysctl.c
check 'watermark_scale_factor_sysctl_handler' include/linux/mmzone.h

# Ensure the two refault kinds have separate VM-zone counters.
for sym in REFAULT ACTIVATE RESTORE; do
  check "WORKINGSET_${sym}_ANON" mm/workingset.c
  check "WORKINGSET_${sym}_ANON" include/linux/mmzone.h
  check "workingset_$(echo "$sym" | tr A-Z a-z)_anon" mm/vmstat.c
done

echo 'PASS: Stage 5 source feature, swap-shadow lifecycle, and VM counter checks'
echo 'NOTE: This is NOT a native kernel compilation, boot test, or locking proof.'
