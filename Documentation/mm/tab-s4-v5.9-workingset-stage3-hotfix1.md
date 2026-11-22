# Tab S4 Linux 5.9-inspired Stage 3 Hotfix 1: swap-exhaustion file-LRU protection

**Source-only until user compiles and tests. Stage 2 and original Stage 3 branches remain untouched.**

## Rationale and evidence limits

The original Stage 3 experiment introduced accepted-file-refault-driven active-file deactivation and an optional cache-trim policy. A high-pressure run showed OOM kills, ANRs, a collapsed page cache and pronounced reclaim stalls. **The user exercised UI transitions and workflows not previously run under Stage 2, so this is not a controlled A/B comparison and does not prove which kernel change caused the difference.** The previous Stage 2 gaming session was smoother, but not a substitute for matched tests.

An identifiable hazard exists in the original Stage 3 source: `shrink_list()` used `sc->deactivate_file` whenever refault activations were observed, including after global swap space had run out. Meanwhile `get_scan_count()` defaults to file-only reclaim when swap slots run out. Together they can accelerate the eviction of frequently referenced file-backed pages.

## Precise change

Under `CONFIG_TAB_S4_V59_REFAULT_DEACTIVATE=y`, `shrink_list()` now overrides the legacy file deactivation ratio only if `get_nr_swap_pages() > SWAP_CLUSTER_MAX`.

At zero or near-zero free swap slots (at most one reclaim cluster, usually 32 base pages), it uses the existing Stage 2 `inactive_file_is_low()` criterion, protecting the active file list when the inactive file list is adequately sized. This guard is **rechecked for each active-file list scan** to handle remaining swap being consumed by other CPUs after `get_scan_count()` runs.

Other Stage 3 features remain unchanged:

- `CONFIG_TAB_S4_V59_CACHE_TRIM=y` **still enabled**. Its conditional file-only scan selection stays in `get_scan_count()` without modification.
- `CONFIG_TAB_S4_V59_REFAULT_DEACTIVATE=y` **still enabled**. Refault counters and adaptive deactivation continue while swap has enough free slots.
- Refault age accounting, reclaim-cost balancing, existing file-cache starvation check, anonymous deactivation and ZRAM/ZSTD settings unchanged.
- No change to Stage 2 or the original Stage 3 branch.

This is a focused hypothesis test, **not a proven fix**. The remaining aggressive reclaim, application workload differences, hardware stalls, and conditional cache-trim can still cause ANRs.

## Validation matrix (no functional change intended outside the guarded path)

| File refault activity | Inactive file list low | Free swap slots | Active file deactivation |
| --- | --- | --- | --- |
| yes | no | > SWAP_CLUSTER_MAX | yes: Stage 3 |
| no | no | > SWAP_CLUSTER_MAX | no: Stage 3 |
| yes/no | yes | any | yes: legacy ratio or Stage 3 |
| yes | no | <= SWAP_CLUSTER_MAX | no: Stage 2 legacy protection |
| no | no | <= SWAP_CLUSTER_MAX | no: Stage 2 legacy protection |

The build kit pins its exact Git commit and confirms all four feature flags, matches the compiled embedded config to `.config`, compares the built `Image.gz` to the installed boot kernel, and preserves Magisk DTB/ramdisk checks.

## Runtime validation and suggested workload

1. Confirm boot+kernel hash and four Kconfig flags after manual Odin flash.
2. Test each workflow explicitly: opening recent apps *from inside SimCity*, switching to home, opening the Trebuchet application drawer, then returning to SimCity, while recording all ANRs and page-pressure samples.
3. Repeat both cold-start and warmed-up sessions; use the same sequence on Stage 2 if seeking controlled causal comparison.
4. Record full-length samples (120/120), PSI, direct-reclaim scans, free swap, available RAM and file-cache residency.
5. If ANRs continue, isolate cache-trim separately in a **subsequent** build, without changing its current enabled setting.
6. Keep a known-working Stage 2 Odin package available for rollback.

Source references: this repository's `mm/vmscan.c` `shrink_list()` and `get_scan_count()`; Linux v5.9 `mm/vmscan.c` file refault and inactive-list management.
