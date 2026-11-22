# Stage 4 investigation: Why Android 17 exhausts ZRAM and stalls on resume

**Branch baseline:** Stage 3 Hotfix 1. No kernel reclaim changes in this branch yet. Keep `CONFIG_TAB_S4_V59_CACHE_TRIM=y`. ZSWAP remains disabled because enabling it repeatedly freezes the tablet. Do not confuse unverified hypotheses with proven regressions.

## Verified historical observations (2026-11-21 and 2026-11-22)

The earlier `swap-investigation.zip` capture, made before Stage 3 Hotfix 1, contains:
- Idle: /proc/swaps 916,516 KiB used (895 MiB), MemAvailable 903 MiB, active+inactive file 916 MiB.
- During SimCity loading: 1,210,932 KiB swapped (1,183 MiB), MemAvailable 506 MiB, file pages 488 MiB.
- City loaded: entire 1,697,700 KiB swapped (1,658 MiB), MemAvailable 152 MiB, file pages 168 MiB.
- SimCity `dumpsys meminfo`: PSS ~1,159,445 KiB, SwapPss ~338,361 KiB, Graphics ~578,588 KiB in the loaded-city capture.
- ZRAM device at full occupancy reported `orig_data_size=1,738,391,552 bytes` and `mem_used_total=433,258,496 bytes`: ~413 MiB real compressed device memory, not 1.66 GiB real RAM.

The prior reclaim audit shows:
- `vm.swappiness=100`, `vm.page-cluster=0`.
- `ro.lmk.use_minfree_levels=true`; legacy file-cache/free thresholds, not equivalent to proactive PSI-thrashing policy.
- `ro.lmk.low=1001`, `ro.lmk.medium=800`, `ro.lmk.critical=0`; `ro.lmk.low=1001` is an Android default, NOT proof that lmkd is inactive.
- 77 cached app processes with `oom_score_adj >=900`; process `VmSwap` is non-proportional and may count shared swapped memory in multiple processes.
- Repeated `OomAdjuster: Not killing cached processes` messages, which AOSP associates with temporary suppression of excessive cached-process kills (possibly configured post-boot grace period); not equivalent to lmkd failure.
- Kernel defconfig already enables `CONFIG_PSI=y` and `CONFIG_MEMCG=y`.

The Stage 3 `dmesg` OOM-time report shows `KgslSharedmem:812368kB` and `SystemHeap:261396kB`. These GPU/system allocations must not be naïvely added to app PSS because of potential overlapping accounting.

The user observed complete hangs when locking SimCity with the power button and attempting to wake it on LineageOS 24 / Android 17, unlike Samsung's factory build. No complete suspend/resume trace from Stage 3 Hotfix 1 exists yet. The Stage 2 and 3 workloads were different, so raw metric comparisons cannot prove causation.

## Investigate before adding behavioural memory-management backports

1. Compare cold/idle background-process retention versus after hours of uptime; inspect ActivityManager `device_config` settings and `lmkd` events. Determine whether cached processes are killed in time.
2. Establish per-process proportional swap attribution using `smaps_rollup` or `dumpsys meminfo <package>` (where available). Do not add up `VmSwap` columns as if all swap entries were unique.
3. Track ZRAM compressed bytes versus logical slots, `pswpin`, `pswpout`, direct-reclaim rates, file-cache footprint and memory PSI at each workload transition.
4. Collect KGSL/ION memory and suspend/resume logs around **SimCity active → physical power sleep → wake**, without an active high-frequency ADB sampler that would interfere with real suspend.
5. On failure, preserve `last_kmsg`/pstore ASAP after reset. Test app-switching and sleep/wake as distinct problems because GPU resume can fail independently of MM.
6. Reproduce the same sequence on Stage 2 only if establishing that Hotfix 1 is materially better or worse. Preserve matching app versions and system uptime.

## Stage 4 candidate directions once evidence is in

- LMKD policy/property and cached-process retention alignment with modern PSI and device constraints, ideally a reversible userspace test before kernel changes.
- Verify Android-facing kernel PSI/PIDFD interfaces required by this userspace `lmkd`. The presence of `/proc/pressure/memory` alone does not establish correct monitor integration.
- Qualcomm graphics buffer lifetime, process allocations and suspend/resume event handling.
- Linux MM backports only for a demonstrated missing behavior that the above cannot address. Keep each hypothesis in a separate branch and preserve Stage 3 Hotfix 1 as baseline.

Local diagnostic kit: `TabS4_Stage4_Memory_Suspend_Diagnostics_Kit.zip` (PowerShell+shell read-only snapshots for idle, city-loaded, recents, launcher, pre-sleep, post-wake and post-reboot). This zip is delivered to the user independently of the Git branch.

References: Android AOSP `lmkd` documentation and `OomAdjuster.shouldKillExcessiveProcesses()`; Linux zram `mm_stat` documentation.
