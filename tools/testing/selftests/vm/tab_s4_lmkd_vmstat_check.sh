#!/system/bin/sh
# Read-only verification that Android lmkd aggregate /proc/vmstat aliases
# exactly match the existing 4.4 per-zone counters in ONE kernel snapshot.
set -eu
test -r /proc/vmstat || { echo "ERROR: /proc/vmstat is unavailable" >&2; exit 2; }
awk '
function fail(reason) { print "FAIL:", reason > "/dev/stderr"; error=1 }
$1=="pgscan_direct" { dir=$2; dir_found++ }
$1=="pgscan_kswapd" { kswapd=$2; ks_found++ }
$1=="pgrefill" { refill=$2; refill_found++ }
$1 ~ /^pgscan_direct_(dma|dma32|normal|high|movable)$/ { dirs+=$2; dir_zones++ }
$1 ~ /^pgscan_kswapd_(dma|dma32|normal|high|movable)$/ { kswaps+=$2; ks_zones++ }
$1 ~ /^pgrefill_(dma|dma32|normal|high|movable)$/ { refills+=$2; refill_zones++ }
END {
 if(dir_found!=1 || ks_found!=1 || refill_found!=1) fail("missing or duplicate aggregate field")
 if(!dir_zones || !ks_zones || !refill_zones) fail("per-zone counters missing")
 if(dir != dirs) fail("pgscan_direct != per-zone sum")
 if(kswapd != kswaps) fail("pgscan_kswapd != per-zone sum")
 if(refill != refills) fail("pgrefill != per-zone sum")
 if(error) exit 1
 printf("PASS: /proc/vmstat aggregates: direct=%s kswapd=%s refill=%s\n", dir, kswapd, refill)
}' /proc/vmstat
