# Stage 4: LMKD PSI/thrashing-policy A/B experiment

Status: **source-directed experiment, not proven on device**.
Base kernel: Stage 3 Hotfix 1 unchanged, including **CONFIG_TAB_S4_V59_CACHE_TRIM=y**.
Keep ZSWAP disabled (it previously froze the tablet); do not alter ZRAM size, compressor or LMKD thresholds in this experiment.

## Evidence from provided snapshots (November 22, 2026, all in EST)

- City-loaded at 11:17: swap 1697668/1697668 KiB, MemAvailable ~51 MiB, memory PSI some avg10 15.41%, 76 sampled processes with oom_score_adj>=900.
- Pre-sleep at 11:19: swap full, MemAvailable ~50.5 MiB, PSI some avg10 48.00%, 73 sampled processes with oom_score_adj>=900.
- Post-wake at 11:22: swap nearly full, PSI some avg10 91.33%, recurrent lowmemorykiller messages reporting 0kB reclaim and failed kill attempts, and kernel OOM kills.
- Captures report ro.lmk.use_minfree_levels=true and ro.lmk.critical=0, ro.lmk.medium=800, ro.lmk.low=1001. sys.lmk.minfree_levels=18432:0,23040:100,27648:200,32256:250,55296:900,80640:950. ro.lmk.kill_heaviest_task=true. sys.lmk.reportkills=1.
- dmesg confirms Android userspace lmkd daemon starts successfully (pid about 996) and kernel memory cgroups and PSI counters are present. These facts alone do **not** prove lmkd successfully registered PSI event monitors or tracks every cached process.
- Samsung's factory firmware did not exhibit the same SimCity hangs, but workload/app versions/Android framework are unmatched; no causal conclusion from raw stage-to-stage rates alone.

## Hypothesis

Android 17 lmkd uses the legacy free/file-cache threshold policy when ro.lmk.use_minfree_levels=true. The current configuration may delay process killing during severe workingset thrashing. Test ro.lmk.use_minfree_levels=false using a **volatile** runtime property override (Magisk resetprop -n) and /system/bin/lmkd --reinit without rebooting system_server or changing kernel behavior. AOSP's GET_LMK_PROPERTY first checks persist.device_config.lmkd_native.*, then ro.lmk.*. Abort the experiment if that persistent override is already defined.

Since this kernel has /proc/pressure/memory, verify whether the Android lmkd runtime actually registers PSI monitors; a readable PSI file alone is insufficient. If LMKD does not use PSI monitoring or behaves poorly, revert immediately. The new strategy may cause more background apps to be killed; app reloads could increase.

## Procedure

The companion user-facing archive **TabS4_Stage4_LMKD_Experiment_Kit.zip** contains:
- tab-s4-stage4-lmkd.sh, tab-s4-stage4-lmkd.ps1 (audit/apply/restore, with original property capture and guarded rollback)
- tab-s4-stage4-watch.sh, tab-s4-stage4-watch.ps1 (lightweight periodic /proc sampling; no sleep test with the sampler running)
- README.md

1. From cold boot with SimCity closed, run PowerShell `tab-s4-stage4-lmkd.ps1 -Mode audit`; record kernel/LMKD status and property overrides.
2. Run `tab-s4-stage4-lmkd.ps1 -Mode apply`; confirm `APPLY`. Requires >256 MiB MemAvailable, running LMKD, no persistent minfree override, and PSI not explicitly disabled.
3. Run audit again. Review LMKD logcat for 'Using psi monitors', initialization failures, event-driven kills, PSI-thrashing indicators, and whether app candidates actually get reaped.
4. Repeat city idle, recents/home and wake use cases while observing RAM, swap, allocstalls and kernel OOM events. Keep test sequence and uptime matched for a later baseline comparison.
5. Restore `tab-s4-stage4-lmkd.ps1 -Mode restore` and confirm legacy setting and runtime status. Reboot also clears the volatile property change.
6. If lmkd reinit fails, restore immediately. Do not use this change as a permanent boot setting until behavior is verified across multiple sessions.

## References

- Android official LMKD documentation: https://source.android.com/docs/core/perf/lmkd
- Android 17 source: https://android.git.googlesource.com/platform/system/memory/lmkd/+/refs/heads/android17-security-release/lmkd.cpp
- Android lmkd.rc on property lmkd.reinit support: https://android.googlesource.com/platform/system/memory/lmkd/+/b3f41d7/lmkd.rc

Future kernel work only after isolating userspace memory-pressure relief: inspect absence of actionable PSI triggers, pidfd and process tracking, and then whether any kernel-v5.9 memory-management path materially improves matched runs.
