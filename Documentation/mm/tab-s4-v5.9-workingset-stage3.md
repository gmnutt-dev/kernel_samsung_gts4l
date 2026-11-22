# Samsung Galaxy Tab S4 SM-T830: Stage 3 refault-guided file-LRU backport

**Status: GitHub source staged for a native WSL compile and controlled device trial. NOT yet compiled/booted/benchmarked on the tablet.**

## Starting point

This branch extends the verified Stage 2 branch `experiment/v5.9-reclaim-cost-stage2` without changing that known-good branch. Stage 2 includes the Stage 1 workingset refault window and Linux 5.9-inspired reclaim-cost weighting. The user measured very smooth SimCity BuildIt gameplay, even with ZRAM at logical capacity, and significantly lower PSI, refault and allocstall rates. A separate Stage 2 run still froze and needed a hard restart, so stability is not yet established.

## Scope

- **`CONFIG_TAB_S4_V59_REFAULT_DEACTIVATE=y`** (depends on both previous stages):
  1. Advance the zone's nonresident-age clock when a file page is activated by an accepted refault. This was missing from Stage 1's refault path; Linux 5.9 does so.
  2. Track successful file refault activations in the owning zone/memory-cgroup's `zone_reclaim_stat`. This vendor kernel lacks Linux 5.9's separate anonymous workingset-shadow machinery, so we deliberately leave its existing anonymous deactivation logic unchanged.
  3. On each `shrink_lruvec()`, snapshot file refault activations under the zone LRU lock, and permit active-file deactivation when refaults have changed **or** the inactive file list has fallen below the original target. Consume only the initial snapshot after reclaim; activity arriving during the scan remains visible next pass.
- **`CONFIG_TAB_S4_V59_CACHE_TRIM=y`** (depends on refault guidance):
  4. In a reclaim pass with no new file activations, prefer old inactive file pages while the inactive file list is large enough to scan. New file refault activations automatically cancel this preference. This is the compact compatible form of Linux 5.9 `cache_trim_mode`.
- The preexisting zone-level file-cache starvation / reclaim-trap check is preserved, and is evaluated before the new cache-trim decision.
- No changes to ZRAM size, ZSTD, ZSWAP, zsmalloc, LMKD, Android userspace, boot DTB, or the ramdisk.

## Why this is a partial backport

Upstream v5.9 maintains node/memcg lruvec nonresident-age, per-file/anon working-set event counters and hierarchical ancestor propagation. This Samsung 4.4-derived kernel stores shadow entries using **zone IDs** and a per-zone nonresident-age clock. Rewriting all of its radix-tree shadow layout, memcg ownership/lifetime, hierarchy and LRU model at once would be risky and difficult to validate against the current excellent Stage 2 behaviour.

Stage 3 keeps that established layout. The new refault guidance applies only to file cache; anonymous pages use the existing `inactive_anon_is_low()` test. This avoids treating absent anonymous-refault counters as evidence of inactivity.

## Concurrency and safety review

- `workingset_refault()` is entered after a new file page has been inserted/charged by `filemap.c`. Both the age update and new refault observation occur without holding the radix-tree lock.
- `workingset_note_file_refault()` takes the same `zone->lru_lock` as `shrink_lruvec()` and cost tracking; the counter is associated with the page's current memcg lruvec.
- The refault snapshot and the final consumed snapshot are protected by the zone LRU lock. If another CPU refaults in between, the new counter remains different from the saved snapshot and the next pass detects it.
- The global low-file-cache check occurs before optional cache trim; this prevents preferentially trimming a file cache already near the zone high watermark.
- Neither the new file-list policy nor the cache-trim logic is compiled into Stage 2 when the new flags are unset. The Stage 2 branch is separately preserved for rollback.

## Limitations and evaluation

This remains an **experimental kernel**, not a full Linux v5.9 memory-management replacement. Native compile, physical-device responsiveness and the memory-pressure failure path are unverified. Stage 3 can regress Stage 2, especially if cache trim over-prefers file reclaim. If a build or test regresses, return to the verified Stage 2 AP TAR.

Evaluation order:
1. Build with `CONFIG_TAB_S4_V59_WORKINGSET=y`, `CONFIG_TAB_S4_V59_RECLAIM_COST=y`, `CONFIG_TAB_S4_V59_REFAULT_DEACTIVATE=y` and `CONFIG_TAB_S4_V59_CACHE_TRIM=y`, ZSWAP disabled. Verify embedded generated config matches `.config`.
2. Confirm installed boot-partition kernel SHA-256 against the artifact, and all four config symbols in `/proc/config.gz`. Preserve Magisk ramdisk and DTB byte-for-byte during repack.
3. Run the same 120-sample memory sampler across idle, loading, panning and sustained full-ZRAM activity, plus logcat. Compare PSI, allocstalls/sec, refaults/sec, direct-reclaim scans/sec, and file-cache residency with Stage 2 (successful full 120-sample log from 2026-11-22).
4. If Stage 3 regresses, build a **Stage 3A isolation variant** with `# CONFIG_TAB_S4_V59_CACHE_TRIM is not set` while keeping `CONFIG_TAB_S4_V59_REFAULT_DEACTIVATE=y`. Do not attempt to change a built-in Kconfig feature live.
5. Only consider replacing the older zone/memcg shadow machinery as a separate, larger research branch.

## Upstream references

- Linux v5.9 `mm/workingset.c`: `workingset_refault()`, `workingset_age_nonresident()`.
- Linux v5.9 `mm/vmscan.c`: `shrink_node()`, `shrink_list()`, `get_scan_count()`.
- This tree's Stage 2 `mm/swap.c`: per-zone/memcg reclaim cost locking and refault-page lookup.
