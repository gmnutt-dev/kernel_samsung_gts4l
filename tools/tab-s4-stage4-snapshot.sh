#!/system/bin/sh
# Snapshot-only diagnostic companion for Samsung Tab S4 Stage 4.
# Does not modify VM, ZRAM, LMKD or kernel configuration.
set -u
phase="$1"
case "$phase" in
  idle|city-loaded|recents|launcher|pre-sleep|post-wake|post-reboot) ;;
  *) echo "Specify: idle city-loaded recents launcher pre-sleep post-wake post-reboot"; exit 2;;
esac
base=/data/local/tmp/tab-s4-stage4-diagnostics
stamp=$(date +%Y%m%d-%H%M%S)
out="$base/$phase-$stamp-$$"
mkdir -p "$out" || exit 1
date > "$out/date.txt"
echo "$phase" > "$out/phase.txt"
for path in /proc/meminfo /proc/vmstat /proc/swaps /proc/uptime \
            /proc/pressure/memory /proc/pressure/io /proc/pressure/cpu \
            /sys/block/zram0/mm_stat /sys/block/zram0/io_stat \
            /sys/block/zram0/comp_algorithm /proc/sys/vm/swappiness \
            /proc/sys/vm/page-cluster /sys/power/state /sys/power/wakeup_count; do
  [ -r "$path" ] || continue
  name=$(echo "$path" | sed 's#^/##;s#/#_#g')
  cat "$path" > "$out/$name.txt" 2>&1 || true
done
(getprop | grep -E '^\[(ro|persist|sys)\.lmk\.|^\[ro\.config\.low_ram\]') > "$out/lmk-properties.txt" 2>&1 || true
(device_config list activity_manager 2>&1) > "$out/device-config-activity-manager.txt" || true
printf 'PID\tOOM_ADJ\tVmRSS_kB\tVmSwap_kB\tState\tNAME\n' > "$out/processes.tsv"
for d in /proc/[0-9]*; do
  [ -r "$d/status" ] || continue
  pid=$(basename "$d")
  adj=$(cat "$d/oom_score_adj" 2>/dev/null) || continue
  stats=$(awk '/^VmRSS:/ {r=$2} /^VmSwap:/ {s=$2} /^State:/ {st=$2} END {printf "%d\t%d\t%s",r,s,st}' "$d/status" 2>/dev/null) || continue
  name=$(cat "$d/comm" 2>/dev/null | tr '\t\r\n' '   ')
  printf '%s\t%s\t%s\t%s\n' "$pid" "$adj" "$stats" "$name" >> "$out/processes.tsv"
done
for p in com.ea.game.simcitymobile_row com.android.launcher3 com.android.systemui system_server; do
  pid=$(pidof "$p" 2>/dev/null | awk '{print $1}')
  [ -n "$pid" ] || continue
  [ -r "/proc/$pid/smaps_rollup" ] && cat "/proc/$pid/smaps_rollup" > "$out/smaps_rollup_$p.txt"
  [ -r "/proc/$pid/wchan" ] && cat "/proc/$pid/wchan" > "$out/wchan_$p.txt"
done
if [ "$phase" = idle ] || [ "$phase" = city-loaded ]; then
  dumpsys meminfo com.ea.game.simcitymobile_row > "$out/simcity-meminfo.txt" 2>&1 || true
fi
dmesg > "$out/dmesg.txt" 2>&1 || true
logcat -b main -b system -b events -b crash -d -v threadtime > "$out/logcat.txt" 2>&1 || true
dumpsys power > "$out/power.txt" 2>&1 || true
if [ "$phase" = post-reboot ]; then
  for p in /proc/last_kmsg /sys/fs/pstore/console-ramoops /sys/fs/pstore/console-ramoops-0; do
    [ -r "$p" ] || continue
    name=$(echo "$p" | sed 's#^/##;s#/#_#g')
    cat "$p" > "$out/$name.txt" 2>&1 || true
  done
fi
archive="$out.tar.gz"
tar -czf "$archive" -C "$base" "$(basename "$out")" 2>/dev/null || exit 1
echo "CAPTURE_OK=$archive"
