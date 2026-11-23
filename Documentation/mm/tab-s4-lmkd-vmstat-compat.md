# Tab S4 Android 17 lmkd vmstat compatibility

Branch: experiment/stage5-lmkd-vmstat-compat, based on experiment/lowram-zsfree-stage5.

## Context

The November 23 SimCity freeze logs show zRAM slots exhausted, available memory reaching zero, aggressive direct reclaim, and kernel OOM kills. The Android 17 lmkd daemon reports using PSI monitors, but no timely successful userspace kill was found in the event capture.

Android 17 lmkd expects aggregate /proc/vmstat fields pgscan_kswapd, pgscan_direct and pgrefill. Samsung 4.4 only printed per-zone counterparts. This branch appends three read-only aliases calculated from the existing per-CPU all_vm_events() snapshot. It does not modify the event enum, the original per-zone values, memory reclaim behavior, or the kernel LMK. AOSP also recognizes the legacy workingset_refault field, which is already present and must not be duplicated.

## Validation and limitations

1. Build ARM64 on the established WSL toolchain. Require CONFIG_TAB_S4_LMKD_VMSTAT_COMPAT=y in the generated and embedded configurations.
2. Preserve the device's original boot, DTB and ramdisk components in Magiskboot repackaging. Flash only after verifying the new kernel payload and retaining a rollback image.
3. On device, run tools/testing/selftests/vm/tab_s4_lmkd_vmstat_check.sh after pushing it. A single /proc/vmstat snapshot must have all three aliases equal to the sums of their corresponding per-zone counterparts.
4. Keep previously enabled PSI userspace new strategy. Enable persist.device_config.lmkd_native.debug=true for the experiment and reinit lmkd; restore debug=false after.
5. Perform a cold-boot SimCity test without Google TV. Collect logcat lowmemorykiller and kernel OOM events, PSI, /proc/swaps, vmstat, meminfo and zram mm_stat. Compare with a prior Stage 5 kernel under the same Android settings.
6. Do not simultaneously adjust swappiness, zRAM algorithm/size or Android thrashing_limit and swap_free_low_percentage. Those require separate A/B comparisons.

This is a compatibility repair, not proof of the freeze root cause. On kernels with working BPF memevents, lmkd may derive reclaim state from BPF instead. Native kernel compilation and tablet runtime tests are not yet performed.
