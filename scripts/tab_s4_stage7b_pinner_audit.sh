#!/system/bin/sh
# Stage 7B one-shot audit. Run as root; no writes or process changes.
set -eu
[ "$(id -u)" = 0 ] || { echo "root required" >&2; exit 1; }
echo '=== PinnerService ==='
dumpsys -t 10 pinner || true
echo '=== DeviceConfig and property ==='
device_config get runtime_native pin_shared_anon_size || true
device_config get runtime_native_boot pin_camera || true
getprop pinner.pin_shared_anon_size
getprop pinner.pin_camera
echo '=== Physical memory ==='
grep -E '^(MemTotal|MemAvailable|Unevictable|Mlocked|CmaTotal|CmaFree):' /proc/meminfo
echo '=== Process locked virtual memory ==='
for p in /proc/[0-9]*/status; do
    [ -r "$p" ] || continue
    if grep -qE '^VmLck:[[:space:]]+[1-9][0-9]*[[:space:]]+kB' "$p" 2>/dev/null; then
        echo "$p"
        grep -E '^(Name|Pid|VmSize|VmRSS|VmLck|VmPin|RssAnon|RssFile|RssShmem):' "$p" 2>/dev/null || true
    fi
done
echo 'VmLck is virtual accounting, not an additive physical-memory total.'
