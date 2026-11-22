# Galaxy Tab S4 SM-T830: Linux 5.9 workingset/reclaim backport investigation

Status: **stage-1 experimental workingset code implemented; NO native compile or runtime validation yet.**

Base branch: `lineage-24.0`, intentionally **not** the separate `experiment/zsmalloc-v5.9-shrinker` branch. This isolates new reclaim experiments from the earlier shrinker experiment.

## Evidence from 2026-11-21 user captures

Source: user-provided `tab-s4-reclaim-audit-20261121-225407.zip`, and earlier gameplay/freeze snapshots.

Post-framework-restart idle snapshot, at about 2260s kernel uptime:
- `MemTotal` = 3,395,412 KiB; `MemAvailable` = 915,184 KiB.
- ZRAM logical swap = 1,697,700 KiB; used = 881,412 KiB.
- ZRAM physical allocation = 189,399,040 bytes, compressed data = 180,692,758 bytes.
- `Cached` = 1,217,692 KiB, `Mlocked` = 301,984 KiB.
- `SUnreclaim` = 229,152 KiB; `KernelStack` = 83,504 KiB; `PageTables` = 137,984 KiB.
- `workingset_refault` cumulative 23,502,923; `pswpin` 970,532; `pswpout` 1,577,474.
- `pgscan_direct_dma` 61,541,319 and `pgscan_direct_normal` 20,882,359 cumulative; `allocstall` 310,903 cumulative. These counts cover activity since the **current kernel boot**, including the previous Android framework failure; do NOT interpret as counts during idle audit.
- At snapshot PSI memory avg10=0.00; no active severe memory pressure.
- 77 snapshot process entries had `oom_score_adj >=900`; 74 had adj 905. These are **cached/background processes**, not apps the user opened. The `VmRSS` and `VmSwap` columns are not unique-memory totals and MUST NOT be summed to claim actual RAM or swap consumption, because shared mappings double-count.
- `init.svc.lmkd=running`.
- `ro.lmk.use_minfree_levels=true`; `sys.lmk.minfree_levels=18432:0,23040:100,27648:200,32256:250,55296:900,80640:950`. Values represent pages and score thresholds under the legacy minfree mode; for 4K pages top threshold is ~315MiB.
- `ro.lmk.low=1001` disables **low-tier** killing, not lmkd as a whole.
- Logcat capture after the restart does not establish whether `lmkd` delivered pressure kills during the preceding freeze. `OomAdjuster: Not killing cached processes` is not proof that `lmkd` was disabled.

Earlier playable city -> frozen snapshot in the **same boot**:
- `MemAvailable`: ~151.7MiB -> ~9.2MiB.
- ZRAM swap already at capacity, no free slots at freeze.
- Memory PSI some 27.24% -> 89.11%.
- `allocstall` 5,008 -> 70,559.
- `workingset_refault` +9,569,971; direct reclaim Normal scan +7,177,793.
- Android 22:34-22:39 watchdog chain: SimCity touch MOVE ANRs, system_server display/WindowManager blocked, framework restarted.
- A similar severe memory-thrash pattern occurred November 20 **before** the experimental ZSMALLOC shrinker change, so the shrinker cannot be the original cause.

## Verified kernel-source comparison

Kernel source:
- Current tree: `mm/workingset.c:238+`, `mm/vmscan.c:2309+`, `include/linux/mmzone.h:218+`.
- Upstream tag: `torvalds/linux` `v5.9`, `mm/workingset.c:282+`, `mm/vmscan.c:2232+`, `include/linux/mmzone.h:275+`.

### Current 4.4-derived implementation

- Per-`zone` `inactive_age` is used in `workingset_eviction` and `workingset_refault`.
- File-cache refault becomes active only if `refault_distance <= NR_ACTIVE_FILE` (same zone).
- `get_scan_count()` balances anon/file using per-zone `recent_scanned[] / recent_rotated[]` rather than measured swapin and file refault costs.
- `get_scan_count()` switches to `SCAN_FILE` when global swap slots are exhausted.
- The configuration has `CONFIG_MEMCG=y`, `CONFIG_MEMCG_SWAP=y`, `CONFIG_PSI=y`, ZRAM/zstd enabled and ZSWAP disabled.

### Mainline Linux 5.9 implementation

- Nonresident age and refault accounting are associated with the relevant `lruvec`, not simply the zone.
- File cache can challenge anonymous pages when swap capacity exists; the working-set size calculation may account for active and inactive anon pages.
- Reclaim records `anon_cost`/`file_cost`, refaults, swapins, and deactivations. `get_scan_count()` uses weighted cost-based balancing.
- Reclaim has `may_deactivate`, `cache_trim_mode`, and `file_is_tiny` policies to avoid pathological file-cache churn.
- **Linux 5.9 also chooses `SCAN_FILE` when there are no available swap slots.** Upstream improvements should reduce thrash before saturation, but do not eliminate that invariant.

Upstream 2020 series:
- https://lists.openwall.net/linux-kernel/2020/05/20/1710 (14-part series rationale, experiments)
- https://lists.openwall.net/linux-kernel/2020/05/20/1713 (cache workingset can challenge anon)
- https://lists.openwall.net/linux-kernel/2020/05/20/1717 (refault/swapin-based LRU cost)
- https://lists.openwall.net/linux-kernel/2020/05/20/1716 (reclaim-root balancing)
- https://lists.openwall.net/linux-kernel/2020/06/16/203 (follow-up fixes required to avoid false activations)
- https://source.android.com/docs/core/perf/lmkd (Android LMKD documentation)

## Proposed implementation: isolated, feature-gated, verified

1. **No changes in current experimental ZSMALLOC branch.** The new branch is based on `lineage-24.0`, which retains the original ZSMALLOC shrinker to eliminate a second changing factor.
2. Map the 2020 14-patch series and associated fixes to the old per-zone `lruvec` and vendor memory-cgroup structure. Review dependencies in `include/linux/mmzone.h`, `mm/workingset.c`, `mm/vmscan.c`, `mm/swap.c`, `mm/swap_state.c`, `mm/memory.c`, and memory-cgroup / vmstat integration.
3. First implement correct workingset challenge/aging **together**. Do not transplant just the refault-distance condition: without aging nonresident information alongside anonymous pages, it can erroneously reactivate stale entries and cause LRU churn (2020-06 follow-up fixes).
4. Then implement refault/swapin-driven relative reclaim costs and active-list deactivation policy. Avoid blindly copying modern zone-to-node or memory-cgroup assumptions into this 4.4 tree.
5. Preserve original behavior via a compile-time experiment switch where feasible and add meaningful, low-cost observability around scan balancing, file/anon reclaim and refault activation.
6. Compile using 48-job WSL toolchain; run static review/regressions; confirm all expected counters and no lock inversions. Do not declare runtime success until a repacked image boots and a repeatable paired workload demonstrates improved PSI and reduced refault/allocstall rates.
7. Test controlled comparisons: existing `lineage-24.0` baseline, experimental ZSMALLOC 5.9 shrinker, and new refault/reclaim version. Hold ZRAM size, swappiness, zstd and Android userspace constant.
8. During a test record same-boot deltas for `workingset_refault`, `workingset_activate`, `pswpin`, `pswpout`, `pgscan_kswapd_*`, `pgscan_direct_*`, `pgsteal_*`, `allocstall`, ZRAM mm/io stats and PSI, plus responsive/ANR outcome.
9. Repack only `Image.gz` into the verified Magisk boot image; preserve `kernel_dtb` and `ramdisk.cpio`; use Odin AP with the existing rollback backup.

## Separate Android-side possibility (not a conclusion)

The audit proves `lmkd` is running, and its settings request the **legacy minfree threshold kill strategy**, rather than the modern thrashing-aware strategy documented for Android 11+. The ROM may still use PSI as a notification source, but `ro.lmk.use_minfree_levels=true` selects the legacy kill logic. A second, separately controlled experiment can assess whether switching to a modern LMKD policy prevents pressure from reaching 95% PSI. **Do not combine that change with a reclaim backport in the same benchmark.**

## Stage 1 implementation on this branch

Source changes: `mm/Kconfig`, `arch/arm64/configs/gts4lwifi_defconfig`, `mm/swap.c`, `mm/workingset.c`, and `mm/vmscan.c`.

The explicitly gated `CONFIG_TAB_S4_V59_WORKINGSET=y` for SM-T830:
- includes anonymous activations in `zone->inactive_age`;
- ages the nonresident clock when active anon/file pages survive reclaim putback;
- when free swap slots remain, allows refaulting file pages to challenge active **and inactive** anonymous pages, as in mainline 5.9;
- falls back to file-only refault qualification when swap is unavailable;
- retains current zone/memcg layout and the original scanned/rotated reclaim balancer.

**This is an adapted first-stage backport, not the full upstream 14-commit 5.9 LRU cost-based balancer.** The cost model remains a separate stage. Experimental code must not be declared working until compiled, booted, and tested.

Build caveat: text of the WiFi defconfig includes `CONFIG_DIRECT_RECLAIM_FILE_PAGES_ONLY=y`, but its Kconfig dependency is `ZSWAP`, which this target disables; the generated `.config` must be used to verify it stays disabled. `CONFIG_INCREASE_MAXIMUM_SWAPPINESS`, `CONFIG_FIX_INACTIVE_RATIO`, and `CONFIG_TIGHT_PGDAT_BALANCE` share that disabled dependency. Stage 1 must not force-enable them.

Rollback requirement: preserve the old Odin tar and a current 64MiB boot partition backup. Repack the new **Image.gz only**, retaining the separated original kernel_dtb and Magisk ramdisk.

