# Samsung Galaxy Tab S4 SM-T830: Stage 5 Linux 5.9-inspired anonymous swap refault and reclaim backport

**Historical Stage 5 status:** original source-stage note, superseded by subsequent experimental builds. This integration branch requires its own native compilation and on-device tests. The separate Stage 3 Hotfix 1 branch is unchanged.

## Why this is needed

The user reported that SimCity BuildIt did not freeze on Samsung factory firmware but repeatedly freezes or triggers watchdog restarts on LineageOS 24 / Android 17. The Android system, app version, graphics stack and workload are not matched, so stock stability does not demonstrate that any single reclaim change caused or will fix the issue.

Prior 2026-11-22 memory-pressure captures consistently show exhausted 1.66 GiB logical ZRAM, critically depleted file cache, sustained page refaults, severe PSI memory stalls, and sometimes OOM kills. The user also reproduced freezes using legacy-minfree and pressure-aware LMKD policies. Upstream Linux 5.9 still chooses SCAN_FILE when swap is exhausted; this patch **does not** change that fundamental limit.

## Feature matrix

| Feature | Linux 5.9 mechanism | Stage 5 adaptation to 4.4 Samsung tree |
| --- | --- | --- |
| Anonymous refault tracking | Swap-cache XArray value shadows and lruvec nonresident age | Fixed-size per-swap-slot encoded shadows, old zone nonresident-age clock |
| Anonymous refault decision | `workingset_refault()` includes file and anon LRUs and distinct activation events | Separate eligibility for swap-backed page, active LRU insertion, distinct `/proc/vmstat` counters |
| Reclaim cost | `lru_note_cost_page()` on restored active pages | Reuses Stage 2 lruvec cost accounting; resolves the swap-cgroup owner ID under RCU before the new page is charged |
| Active LRU aging | Refault signals and inactive-list targets | Stage 3 file and new Stage 5 anon signals per zone/memcg reclaim_stat |
| Retry | `skipped_deactivate` and `force_deactivate` when reclaim cannot free pages | One bounded retry, disabled when ZRAM has too few free slots to avoid Stage 3 regression |
| Scheduling | Explicit `cond_resched()` in reclaim scan loop | Gated Stage 5 scheduling point |
| Background reclaim | `vm.watermark_scale_factor` sets kswapd low/high spacing | Same proportional gap formula adapted to retain Samsung `extra_free_kbytes`, default 50 (0.5%), range 1–1000 |

This is an intentionally constrained backport. It is **not a full upstream 5.9 XArray, memory-cgroup hierarchy, watermark-boost and LRU rewrite**.

## Core design and slot lifetime

1. `CONFIG_TAB_S4_V59_ANON_WORKINGSET=y` adds `swap_info_struct.tab_s4_anon_shadow` (`unsigned long[maxpages]`). Allocated via `vzalloc` at swapon, freed at swapoff and all error paths. On the Tab S4's ~414K 4 KiB ZRAM slots this is approximately 3.2 MiB extra physical memory.
2. In `__remove_mapping()`, **only reclaimed** swap-backed pages receive an eviction shadow from `workingset_eviction()`. The slot metadata is stored before the swapcache radix-tree mapping is removed.
3. `swap_entry_free()` zeroes metadata when `usage==0`, including slot recycling.
   The eviction-store path uses `WRITE_ONCE` while the swap-cache `tree_lock` is held; it must not acquire `swap_info->lock`, since `free_swap_and_cache()` acquires the locks in the opposite order. A live swap-cache reference prevents slot recycling until the store completes. The swap-in take path and slot-free cleanup are serialized by `swap_info->lock`.
4. In `__read_swap_cache_async()`, on successful new swap-cache insertion the stored shadow is consumed **once**, evaluated by `workingset_refault()`, and accepted pages are placed on the active anonymous LRU using `lru_cache_add()`. Ordinary swap-ins still use `lru_cache_add_anon()`.
5. Anonymous and file refaults have different residency windows (from v5.9's model) and new counters are separately exposed as `workingset_refault_anon`, `workingset_activate_anon`, `workingset_restore_anon` in `/proc/vmstat`. Existing `workingset_refault` remains file-only for comparison with Stage 2/3.

**Safety limitation:** Unlike v5.9's fully hierarchical memcg lruvec, this legacy kernel has zone-based shadow timestamps. Refault activation and reclaim-cost feedback use the current swap-cgroup owner ID before the incoming page is charged, falling back to the page/root lruvec when swap ownership is unavailable. Ancestor-propagated nonresident ages remain absent, so this is not identical to upstream's full hierarchy.

## Reclaim and watermarks

- `CONFIG_DIRECT_RECLAIM_FILE_PAGES_ONLY` explicitly **not set** in the Wi-Fi defconfig, and the build kit requires it to remain disabled in generated and embedded configurations. It was also already Kconfig-ineligible because its original definition `depends on ZSWAP`, while ZSWAP is disabled; the explicit state prevents ambiguity.
- `CONFIG_TAB_S4_V59_RECLAIM_RETRY=y` adds a yielding point and a **single** failed-reclaim forced-deactivation retry when free swap is > `SWAP_CLUSTER_MAX` pages. This is intentionally more conservative than upstream at swap exhaustion.
- `CONFIG_TAB_S4_V59_WATERMARK_SCALE=y` exposes `/proc/sys/vm/watermark_scale_factor`; default 50 means 0.5% of each zone's managed pages as the minimum background reclaim watermark gap. The legacy min/4 gap and the Samsung `extra_free_kbytes` contribution remain respected. The upstream v5.9 baseline default is 10; 50 is an experimental device-specific setting and can be changed live.
- **`CONFIG_TAB_S4_V59_CACHE_TRIM=y` stays enabled**. The Stage 3 Hotfix 1 active-file swap-exhaustion guard was later explicitly removed from Stage 5; other Stage 5 anonymous retry checks still have swap availability guards. ZSWAP, ZRAM writeback and ZRAM size are unchanged.

## Engineering validation and test matrix

No native compile or tablet-boot validation is claimed. The supplied build kit pins the source commit, builds using the established Linaro GCC 6.5.0 toolchain, checks the generated and **embedded** kernel configs and produces a signed-off manifest + Odin package with Magisk/ramdisk/DTB preservation checks.

Before flashing, **require**:
- a clean native WSL `Image.gz`/`Image.gz-dtb` build;
- embedded `CONFIG_TAB_S4_V59_ANON_WORKINGSET=y`, `CONFIG_TAB_S4_V59_RECLAIM_RETRY=y`, `CONFIG_TAB_S4_V59_WATERMARK_SCALE=y`, as well as all Stage 1–3 flags;
- embedded `# CONFIG_DIRECT_RECLAIM_FILE_PAGES_ONLY is not set` (or absent symbol) and ZSWAP disabled;
- unchanged ramdisk/DTB versus device backup.

Recommended runtime tests:
1. Device boots, all configs and kernel hash match, and `cat /proc/sys/vm/watermark_scale_factor` returns 50.
2. Under swapping, `workingset_refault_anon` and `workingset_activate_anon` increase. No unexpected swapfile allocator/slot warnings in dmesg.
3. At steady 10-second sampling, compare per-second refaults (file AND anon), allocstall, direct scans, kswapd scans, PSI, available RAM and ZRAM swap used.
4. Repeat **the same** SimCity sequence: foreground active city, idle city, recent apps, home/application drawer, screen-off and wake. A cached-process LMKD property change is a separate experiment, not mixed into this kernel A/B test.
5. Keep the Stage 3 Hotfix 1 Odin package available for immediate rollback. A native build or boot failure means Stage 5 remains unvalidated.

## Source references

- Upstream Linux v5.9 `mm/workingset.c`, `mm/swap_state.c`, `mm/vmscan.c`, `mm/page_alloc.c`
- Stage 3 Hotfix 1 base: `experiment/v5.9-workingset-stage3-hotfix1`.
