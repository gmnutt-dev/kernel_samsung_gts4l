#!/system/bin/sh
# Tab S4 Stage 4 rapid read-only diagnostic snapshot, V2.
# No VM/ZRAM/LMKD modifications. Progress printed as stages complete.
set -u
phase="$1"
case "$phase" in
  idle|city-loaded|recents|launcher|pre-sleep|post-wake|post-reboot) ;;
  *) echo 'ERROR: use idle|city-loaded|recents|launcher|pre-sleep|post-wake|post-reboot' >&2; exit 2;;
esac
base=/data/local/tmp/tab-s4-stage4-diagnostics
now="$(date +%Y%m%d-%H%M%S)"
out="$base/$phase-$now-$$"
mkdir -p "$out" || exit 1
progress() { printf 'PROGRESS [%s] %s\n' "$phase" "$1"; }

# Bound Android services. If no timeout applet exists, skip those commands.
limited() {
  limit="$1"; shift
  if command -v timeout >/dev/null 2>&1; then
    timeout "$limit" "$@"
  elif command -v toybox >/dev/null 2>&1; then
    toybox timeout "$limit" "$@"
  elif command -v busybox >/dev/null 2>&1; then
    busybox timeout "$limit" "$@"
  else
    echo 'Skipped: no timeout utility available' >&2
    return 124
  fi
}
printf '%s\n' "$phase" > "$out/phase.txt"
date > "$out/date.txt" 2>&1 || true

progress '1/6 lightweight kernel and ZRAM counters'
for item in \
    /proc/uptime /proc/meminfo /proc/vmstat /proc/swaps /proc/zoneinfo \
    /proc/pressure/memory /proc/pressure/io /proc/pressure/cpu \
    /sys/block/zram0/mm_stat /sys/block/zram0/io_stat \
    /sys/block/zram0/comp_algorithm /sys/block/zram0/disksize \
    /proc/sys/vm/swappiness /proc/sys/vm/page-cluster \
    /proc/sys/vm/min_free_kbytes /proc/sys/vm/extra_free_kbytes \
    /sys/power/state /sys/power/wakeup_count \
    /sys/kernel/debug/suspend_stats /sys/kernel/wakeup_reasons/last_resume_reason; do
  if [ -r "$item" ]; then
    name=$(printf '%s' "$item" | tr '/' '_')
    cat "$item" > "$out/$name.txt" 2>&1 || true
  fi
done

progress '2/6 process RSS/swap and LMK properties (single awk pass)'
(getprop | grep -E '(^\[(ro|persist|sys)\.lmk\.|^\[ro\.config\.low_ram\]|^\[dalvik\.vm\.|^\[ro\.sys\.fw\.|^\[sys\.boot_completed\])') > "$out/properties-memory.txt" 2>&1 || true
printf 'PID\tOOM_ADJ\tVmRSS_kB\tVmSwap_kB\tState\tNAME\n' > "$out/processes.tsv"
# One awk reads all /proc/<PID>/status files; no subprocess-per-PID loop.
awk '
function emit(   p,a,q) {
  if (!pid) return
  p="/proc/" pid "/oom_score_adj"
  a="?"
  if ((getline q < p) > 0) a=q
  close(p)
  printf "%s\t%s\t%d\t%d\t%s\t%s\n", pid,a,rss,swap,state,name
}
FNR == 1 {
  emit()
  split(FILENAME,f,"/")
  pid=f[3]; rss=0; swap=0; state="?"; name="?"
}
/^Name:/ {name=$2}
/^State:/ {state=$2}
/^VmRSS:/ {rss=$2}
/^VmSwap:/ {swap=$2}
END {emit()}
' /proc/[0-9]*/status >> "$out/processes.tsv" 2> "$out/processes-errors.txt" || true
(ps -A -o USER,PID,PPID,STAT,RSS,NAME 2>&1 || ps -A 2>&1) > "$out/ps.txt" || true

progress '3/6 selected app process details'
for proc in com.ea.game.simcitymobile_row com.android.launcher3 com.android.systemui system_server; do
  pid=$(pidof "$proc" 2>/dev/null | awk '{print $1}')
  [ -n "$pid" ] || continue
  [ -r "/proc/$pid/smaps_rollup" ] && cat "/proc/$pid/smaps_rollup" > "$out/smaps_rollup_$proc.txt" 2>&1
  [ -r "/proc/$pid/status" ] && cat "/proc/$pid/status" > "$out/status_$proc.txt" 2>&1
  [ -r "/proc/$pid/wchan" ] && cat "/proc/$pid/wchan" > "$out/wchan_$proc.txt" 2>&1
done

progress '4/6 time-limited Android service diagnostics'
if [ "$phase" = idle ]; then
  limited 4 device_config list activity_manager > "$out/device-config-activity-manager.txt" 2>&1 || true
  limited 4 device_config list lmkd_native > "$out/device-config-lmkd.txt" 2>&1 || true
elif [ "$phase" = city-loaded ] || [ "$phase" = pre-sleep ] || [ "$phase" = post-wake ]; then
  limited 8 dumpsys meminfo com.ea.game.simcitymobile_row > "$out/simcity-dumpsys-meminfo.txt" 2>&1 || true
fi
limited 5 dumpsys power > "$out/power.txt" 2>&1 || true

progress '5/6 bounded kernel/Android logs'
case "$phase" in post-wake|post-reboot) lines=6000;; *) lines=2000;; esac
limited 8 logcat -b main -b system -b events -b crash -d -v threadtime -t "$lines" > "$out/logcat.txt" 2>&1 || true
limited 5 dmesg > "$out/dmesg.txt" 2>&1 || true
if [ "$phase" = post-reboot ]; then
  for item in /proc/last_kmsg /sys/fs/pstore/console-ramoops /sys/fs/pstore/console-ramoops-0; do
    if [ -r "$item" ]; then
      name=$(printf '%s' "$item" | tr '/' '_')
      cat "$item" > "$out/$name.txt" 2>&1 || true
    fi
  done
fi

progress '6/6 packaging archive'
archive="$out.tar.gz"
if tar -czf "$archive" -C "$base" "$(basename "$out")" 2>/dev/null; then
  printf 'CAPTURE_OK=%s\n' "$archive"
else
  echo 'ERROR: archive creation failed' >&2
  exit 1
fi
