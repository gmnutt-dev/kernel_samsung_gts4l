# Tab S4 SM-T830 | Stage 2: refault-aware reclaim-cost balancing

**Status: source implementation staged, NOT natively compiled, booted, or performance-validated.**

## Baseline and problem

Based on the Stage 1 implementation on `experiment/v5.9-refault-reclaim`. The user's original kernel still reaches 1,697,700 KiB of used logical ZRAM, then suffers severe file-cache refaults, direct-reclaim stalls, and temporary device-wide UI hangs. Stage 1 (`CONFIG_TAB_S4_V59_WORKINGSET=y`) produced exceptionally smooth gameplay when not under extreme pressure, and demonstrated the ability to recover from some freezes, but ZRAM eventually filled.

The Stage 1 install was correctly built and flashed. Four independent SHA-256 comparisons matched the running boot image's extracted kernel to the WSL `Image.gz`. Its `/proc/config.gz` was misleading because `kernel/Makefile` hard-coded a *different* defconfig. See below.

## Stage 2 changes

Linux 5.9 upstream references:
- `torvalds/linux@v5.9: mm/vmscan.c` (relative LRU costs in `get_scan_count`)
- `torvalds/linux@v5.9: mm/swap.c` (`lru_note_cost`, `lru_note_cost_page`)
- `torvalds/linux@v5.9: mm/workingset.c` (refault cost for restored pages)
- 2020 original series: https://lists.openwall.net/linux-kernel/2020/05/20/1710

New `CONFIG_TAB_S4_V59_RECLAIM_COST=y` depends on Stage 1 and `SWAP`, enabled in **gts4lwifi_defconfig only**. It:
1. Adds per-zone/per-memcg `anon_cost`/`file_cost` to `zone_reclaim_stat`. All modifications and reads occur under the existing `zone->lru_lock`.
2. Counts pages submitted to I/O by `shrink_page_list`, passes the count back through `shrink_inactive_list`, and charges the source LRU cost.
3. Charges the **file** LRU cost when a previously active file page is restored by `workingset_refault`, once page-cache insertion and memcg charging are complete.
4. Decays costs once their sum exceeds one quarter of the corresponding zone/memcg LRU size, using the same basic principle as Linux 5.9.
5. Uses weighted relative costs rather than historical scan/rotation rates during fractional anon/file reclaim. Avoids the unconditional legacy `SCAN_FILE` shortcut based solely on an ample inactive-file list. It still honors swap exhaustion, boost and tiny-file conditions in the old tree.
6. Leaves the old scan/rotation code in place as a compile-time fallback when `CONFIG_TAB_S4_V59_RECLAIM_COST=n`. No ZRAM, zsmalloc, zswap, compression or LMKD configuration changes are included.
7. Fixes `kernel/Makefile` to embed `$(KCONFIG_CONFIG)` instead of `arch/arm64/configs/msmcortex-perf_expanded_defconfig`, restoring an accurate `/proc/config.gz` and `scripts/extract-ikconfig` output.

**Limitations:** This is the *cost-balancing* part of Linux 5.9's LRU improvements, adapted to the vendor kernel. It is not a wholesale v5.9 mm replacement. The upstream `may_deactivate`/`cache_trim_mode` policy and node-level lruvec restructuring remain out of scope. When ZRAM truly has no free swap slots, the kernel will still restrict reclaim to file-backed pages.

## Locking & correctness audit

- `mm/swap.c:lru_note_cost()` expects an already held `zone->lru_lock`.
- Called from `mm/vmscan.c:shrink_inactive_list()` only after acquiring that lock.
- `lru_note_cost_page()` acquires the matching zone lock before retrieving the page's memcg lruvec, and releases it after updating its cost. The `filemap.c:add_to_page_cache_lru()` caller invokes `workingset_refault()` after page-cache insertion and `mem_cgroup_commit_charge()`, outside the page-cache tree lock and outside the zone LRU lock.
- `get_scan_count()` samples the cost counters while holding the zone LRU lock, then performs arithmetic after releasing it.
- The LRU cost bookkeeping uses the old per-zone lruvec model; unlike v5.9, it does not propagate cost to ancestor lruvec structures.
- `shrink_page_list()` additional `ret_nr_pageout` out-parameter is supplied by all three call sites, with temporary accumulators initialized to zero.
- When the feature is disabled, the helpers are no-ops and the original scan balance executes unchanged.

## Build and validation requirements

The companion Stage 2 build kit pins a specific commit on this branch, retains its own build tree and backup directory, validates BOTH stage features and disabled ZSWAP, and extracts the embedded config from the **built** `Image.gz` before allowing packaging. Use the paired Stage 2 Magisk ADB scripts; the original 64-MiB boot partition must be backed up. Magisk repacking must retain the existing `kernel_dtb` and Magisk `ramdisk.cpio` byte-for-byte.

Before flashing:
- WSL Linaro 6.5 native compile must succeed with no warnings from the changed source paths.
- `scripts/extract-ikconfig` on output `Image.gz` must show `CONFIG_TAB_S4_V59_WORKINGSET=y` and `CONFIG_TAB_S4_V59_RECLAIM_COST=y`.
- Check Magisk unpack-repack verification; do not flash if hashes differ or the output is oversized.

After flashing:
- Compare the boot partition's extracted kernel SHA-256 against built `Image.gz`.
- Check `/proc/config.gz` for **both** options.
- Run an otherwise unchanged SimCity session with 5-second sampler, comparing PSI, free swap, pageout, refault/activation rates, direct-reclaim scans and allocstall rates to Stage 1.
- Keep the Stage 1 Odin TAR as a one-step rollback. If the system shows new kernel instability, return to Stage 1.

## Change control

Stage 1 branch is not modified. Stage 2 branch is isolated. Native build and runtime validation remain pending on the actual WSL installation/SM-T830.
