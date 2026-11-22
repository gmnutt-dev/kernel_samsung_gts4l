# Samsung Galaxy Tab S4 SM-T830: Linux 7.3.5 legacy-LRU experiment

Status: **source implementation only**. No native cross-compile, boot, or SimCity runtime
validation has been performed. This is not a complete Linux 7.3.5 MM backport.

## Provenance

* Fork: `gmnutt-dev/kernel_samsung_gts4l`
* Base parent: `lineage-24.0` at `6deb5affa1694e36421829b586d4fe80923433dd` (4.4.302-cip112 vendor kernel).
* Source for initial port foundation: previous `experiment/v5.9-reclaim-cost-stage2`.
  The previous experiments were **read as reference material, not merged**;
  this candidate is a fresh child of the baseline commit.
* Modern reference: `gregkh/linux` `v7.3.5`,
  `mm/vmscan.c` functions `prepare_scan_control()`, `get_scan_count()`,
  `shrink_lruvec()`, and `mm/workingset.c` `workingset_refault()`.
* The original Stage 3 / Hotfix 1 source remains untouched.

## Feature mapping and exact limits

| Kconfig | Old-tree mechanism | Reference from 7.3.5 |
| --- | --- | --- |
| TAB_S4_V59_WORKINGSET | Expanded file refault residency window; ages anon references under old zone clock | Workingset competing with anon |
| TAB_S4_V59_RECLAIM_COST | Per-zone/memcg I/O and restored-file costs used for balanced scanning | Relative reclaim-cost policy |
| TAB_S4_V73_REFAULT_AGE | Increment nonresident clock on **accepted** file refault | `workingset_refault()` |
| TAB_S4_V73_COST_SIGNALS | Weight actual pageouts and restored file refaults ×32; rotations ×1 | `prepare_scan_control()`'s IO×cluster plus rotations |
| TAB_S4_V73_FILE_CACHE_GUARD | Only force SCAN_ANON for tiny file cache when inactive anon and swap headroom exist | `file_is_tiny` guard |
| TAB_S4_V73_RECLAIM_YIELD | `cond_resched()` per completed LRU scan batch | `shrink_lruvec()` scheduling point |

The 7.3.5 model uses monotonic per-lruvec VM event deltas, rolling averages, folios,
node-level LRU vectors, memory-cgroup hierarchy, and independent anonymous shadow
refaults. **None of those internals are transplanted here**. The original
4.4 lruvec cost counters and their decay are retained. Thus the relative
weights are a carefully limited *adaptation*, not exact 7.3.5 semantics.

**Excluded deliberately:** Stage 3's `REFAULT_DEACTIVATE`, `CACHE_TRIM`, and
Hotfix 1 file deactivation override, plus Stage 5's anonymous shadow storage,
force-deactivation retry and watermark changes. MGLRU, DAMON and folio rewrite
are not compatible drop-in units. In particular, this experiment does not
claim to prevent SCAN_FILE when all swap slots are occupied.

The stale defconfig request `CONFIG_DIRECT_RECLAIM_FILE_PAGES_ONLY=y` was
changed explicitly to `# ... is not set`. It had already been unavailable
with ZSWAP disabled due to its Kconfig dependency. No ZRAM/zsmalloc compressor,
ZSWAP, cgroup limit, ramdisk, DTB or Android LMKD changes are included.

## Source-control and validation contract

Source check: run `bash tools/tab-s4-v73-source-check.sh` in the branch root.
This verifies feature gates and ensures the Stage 3 policies are not present.
It does **not** replace a native compile or memory-stress test.

1. Obtain an independent clean WSL build tree for this branch; do not reuse
   Stage 3 object files. Use the previously working Linaro GCC 6.5.0 toolchain.
2. Build SM-T830 with `gts4lwifi_defconfig`. Confirm the generated
   `.config` has all six experiment gates enabled and ZSWAP disabled.
   **Check that `CONFIG_DIRECT_RECLAIM_FILE_PAGES_ONLY` is absent/disabled.**
3. Extract the embedded `/proc/config.gz` equivalent from compiled
   `Image.gz` using `scripts/extract-ikconfig`. Compare to generated
   `.config` before packaging; `kernel/Makefile` fixes the original hardcoded
   config embedding mismatch.
4. Repack only `Image.gz` in a verified Magisk boot image while preserving
   `kernel_dtb` and `ramdisk.cpio` byte for byte. Preserve original boot
   partition backup and known-booting Stage 2 Odin TAR for rollback.
5. Verify the boot partition kernel hash against the built `Image.gz` and
   verify active config values before performance testing.
6. Compare A/B within the same Android userspace and workload (idle, launch,
   pan, idle city, recent apps, home, drawer, return, screen off/wake).
   Log 5s samples of `workingset_refault`, `workingset_activate`,
   `workingset_restore`, direct/kswapd scans, `allocstall`, `pswpin`,
   `pswpout`, PSI some/full, swap free, zram `mm_stat`/`io_stat`,
   `nr_inactive_file`, `nr_active_file`, `nr_inactive_anon`,
   and all ANRs/reboots. Compare same-boot deltas, not absolute counters.

## Isolation sequence

* **A:** all four TAB_S4_V73_* gates disabled; Stage 2-equivalent foundation.
* **B:** enable REFAULT_AGE only.
* **C:** enable COST_SIGNALS also.
* **D:** enable FILE_CACHE_GUARD and RECLAIM_YIELD (full experimental config).
* Compare again with the unmodified `lineage-24.0` baseline.
* If one option regresses, disable it alone and rebuild; none are runtime tunables.

The earlier Stage 2 run was smooth but still eventually froze; there is no
validated permanent solution yet. Critical design caveat: with exhausted
ZRAM, upstream 7.3.5 still cannot reclaim ordinary anonymous pages to swap.
Avoid claims that the modern weighting algorithm alone eliminates freezes.
