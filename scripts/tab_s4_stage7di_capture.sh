#!/system/bin/sh
# Read-only, one-time stage7 diagnostics; run as root before/during/after ad.
date; cat /proc/uptime
echo "=== memory ==="; cat /proc/meminfo; cat /proc/swaps
cat /sys/block/zram0/mm_stat /sys/block/zram0/disksize 2>/dev/null
echo "=== pressure ==="; cat /proc/pressure/memory /proc/tab_s4_stage7i_psi 2>/dev/null
echo "=== VM ==="; cat /proc/vmstat
echo "=== Stage7 parameters ==="
for p in /sys/module/*/parameters/stage7d_* /sys/module/*/parameters/stage7e_periodic_stats /sys/module/zram/parameters/stage7g_* /sys/module/*/parameters/stage7h_kgsl_stats /sys/module/*/parameters/stage7_pool_stats; do
    [ -r "$p" ] || continue
    echo "$p"; cat "$p"
done
echo "=== ION ==="; timeout 8 cat /sys/kernel/debug/ion/heaps/system 2>/dev/null
echo "=== dma-buf ==="; timeout 8 cat /sys/kernel/debug/dma_buf/bufinfo 2>/dev/null
echo "=== LMKD monitors ==="
for pid in $(pidof lmkd); do
    echo "lmkd PID $pid"; ls -l /proc/$pid/fd; cat /proc/$pid/status
done
echo "=== LMKD policy ==="; getprop | grep -E 'lmk|low_ram|zram'
timeout 5 device_config list lmkd_native
echo "=== codec ==="; timeout 8 dumpsys -t 6 media.codec
echo "=== resources ==="; timeout 8 dumpsys -t 6 media.resource_manager
echo "=== slab ==="; cat /proc/slabinfo 2>/dev/null
echo "=== crash ==="; logcat -d -b crash -v threadtime
