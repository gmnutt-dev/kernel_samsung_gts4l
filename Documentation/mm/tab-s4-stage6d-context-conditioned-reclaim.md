# Samsung Galaxy Tab S4 Stage 6D: context-conditioned reclaim

**Branch:** `experiment/stage6d-context-conditioned-reclaim`  
**Direct foundation:** `experiment/stage6c-pressure-directed-reclaim`  
**Device:** SM-T830 WiFi, Samsung/Linux 4.4.302-derived Android kernel

## Motivation

A SimCity test on Stage 6B/6C recovered much better interactivity than the
earlier Stage 3/5 builds, but first launch after reboot or app-switches could
still stall the launcher and Android system process. ANR and memory-pressure
telemetry shows substantial cache refaults, ZRAM exhaustion, and background
reclaim. Stage 6C had not reclaimed any pages in an earlier measurement, so
benefit must not be attributed to its extra reclaim pass.

The Anisa-inspired hypothesis is that **current resource conditions and
event history** may be a better indicator of what to protect than a fixed
two-minute age window. Explicit app/page ownership "addresses" are not
necessary to build this experimental preference policy. The policy is not
a claim of application-awareness or new RAM capacity.

## What is different from Stage 6B

Stage 6B remains intact, with its independent score fields and 25% bound.
Stage 6D adds an independent history in each existing
`zone_reclaim_stat`, per (zone, memcg LRU vector, anon/file):

- short score: unsigned 0..15;
- long score: unsigned 0..15;
- reclaim-quality penalty: unsigned 0..8;
- timestamps for lazy decay, recent refault, last admitted refault credit.

No per-page attributes, per-access hooks, process-to-page mapping, heap
allocations or timers are introduced.

### Activity inputs (existing event/reclaim paths)

- Accepted file/anon refaults: admit at most one credit per quarter-second
  per context (not one credit per refault), adding +3 short and +1 long.
  An accepted refault after >2 seconds of inactivity counts as a context
  renewal hint. This is *not* proof of an Android foreground transition.
- Active-list rotation when at least half of 16+ examined pages were
  referenced: short +1.
- Productive inactive reclaim when 75%+ of 16+ pages were reclaimed:
  short -1 and quality penalty +2 (up to 8).
- Inefficient reclaim when 25% or less were recovered: quality penalty -1.
  Quality history lazily returns to neutral along with the long score.

Stage 6D does not add a duplicate atomic64 counter on every refault;
it reports the rate-limited credit events, while Stage 6B still
reports the exact refault event counters.

### Adaptive age from live swap headroom

| Free logical swap slots | Fast-score decay | Slow-score decay |
|---|---:|---:|
| More than 50% | 1 point / 2 s | 1 point / 16 s |
| 25% to 50% | 1 point / 1 s | 1 point / 8 s |
| 25% or less | 1 point / 0.5 s | 1 point / 4 s |

Ages are recomputed **lazily** when the existing fault/reclaim paths observe
a context; there is no periodic timer. All score data are owned by the
existing zone LRU lock. A pressure-band transition changes the future
effective decay when history is next observed; it is not a time-accurate
integral of prior pressure levels.

### Combined context score

The scan preference is computed from:

```
short + long
+ 2 if the class has refaulted in the last 2 seconds
- 3 if the other class has active refaults and substantially more short credit
- reclaim-quality penalty (0..8)
```

The final percentage is clamped to [0, 20] by default, never above 25
(`vm.tab_s4_stage6d_cap` = 0..25). The existing `get_scan_count()`
applies a percentage only in **normal global reclaim**, keeping at least 75%
of its selected scan targets. The same Stage 6B emergency bypasses,
low-swap guard, memory boosting exclusion, forced-retry exclusion,
reclaim priority guard and cgroup reclaim exclusion remain in force.

All original Stage 5 allocation, swap, zsmalloc, LMKD and Android
`/proc/vmstat` compatibility paths remain unchanged.

## Runtime modes

Stage 6D compiles with `CONFIG_TAB_S4_STAGE6D=y` but **boots disabled**.
Mode 0 does not even update 6D's new event scores; mode 1 collects 6D
shadow telemetry but leaves the Stage 6B policy untouched; mode 2 applies
6D's score policy in place of 6B's scan preference.

```sh
# Stage 6B alone, without Stage 6D event score maintenance:
su -c 'echo 0 > /proc/sys/vm/tab_s4_stage6d_mode'
su -c 'echo 1 > /proc/sys/vm/tab_s4_stage6b_protection'
su -c 'echo 0 > /proc/sys/vm/tab_s4_stage6c_enable'

# Stage 6D shadow telemetry, with 6B decisions unchanged:
su -c 'echo 1 > /proc/sys/vm/tab_s4_stage6d_mode'

# Context-conditioned Stage 6D replacing the 6B scan preference:
su -c 'echo 20 > /proc/sys/vm/tab_s4_stage6d_cap'
su -c 'echo 2 > /proc/sys/vm/tab_s4_stage6d_mode'

# Immediate rollback without rebuilding:
su -c 'echo 0 > /proc/sys/vm/tab_s4_stage6d_mode'

# Diagnostics:
su -c 'cat /proc/tab_s4_stage6d'
su -c 'cat /proc/tab_s4_stage6b'
```

Stage 6D override requires Stage 6B protection to be enabled. Stage 6C
remains **off** by default and is not required for 6D behavior.
If Stage 6C is later enabled together with 6D, its context-class
selection also consults 6D scores; the pre-existing Stage 6C
pressure-trigger and watermark restrictions still apply.

Stage 6D counters in `/proc/tab_s4_stage6d` are lifetime totals.
They distinguish refault credits, renewal hints, reclaim quality
signals, pressure bands, class preference readouts and actual scan
**targets deferred**, not pages proven retained. Existing Stage 6B
counters describe only 6B policy deferrals when 6D takes over.

## Validation plan

1. Native cross-compile with `gts4lwifi_defconfig`, using the same
   pinned Linaro GCC 6.5 toolchain and unchanged Stage 5/6B settings.
   Compare the kernel's embedded IKCONFIG to generated `.config`.
2. Repack using the previously established on-tablet Magiskboot
   `unpack -n -h`, `repack -n` and verify boot/RAMDISK/DTB hashes.
   Never flash automatically.
3. Start with `6B=1,6C=0,6D=0`. Reproduce a SimCity first-launch and
   launcher-transition scenario. Then reboot and use `6B=1,6C=0,6D=1`.
   Do not simultaneously alter ZRAM size, LMKD policy or swap compression.
4. Measure launch/return-to-Home time, ANR count, per-zone and aggregate
   pgscan/pgsteal, PSI some/full, swap occupancy, workingset file/anon
   refaults, CPU use of kswapd and 6D deferral counters.
5. If app switching worsens, disable 6D immediately by its sysctl.
   Compare trials across at least two matching cycles.

Mode 0 retains the Stage 6B scan-target algorithm without 6D event-score
maintenance. A cheap branch remains in each existing 6B event handler.
Mode 1 updates the independent 6D history but applies Stage 6B scan
preferences. Changing runtime mode does not reset independent history;
reboot between comparable A/B trials.

## Limits and gaps

- Activity is per zone/memcg LRU vector and anonymous/file class, not
  inherently per Android application; applications sharing an LRU vector
  cannot be separated by this version. Confirm cgroup assignments before
  claiming per-app behavior.
- These policies don't prevent logical ZRAM slot exhaustion or guarantee
  home-screen latency under severe memory pressure. Android ANR traces
  and LMKD choices remain important independent data.
- No native kernel compilation, device boot, Android app-switch test
  or proof of performance parity has been performed by this patch author.
- A small number of extra event-path writes are introduced. The refault
  credit limiter keeps most Stage 6D accounting off the hot path.
- More detailed contextual protection may eventually require an explicit
  Android userspace priority signal, but that is intentionally not part
  of this conservative experiment.

## Static checks

```sh
bash tools/tab-s4-stage5-source-check.sh
bash tools/tab-s4-stage6b-source-check.sh
bash tools/tab-s4-stage6c-source-check.sh
bash tools/tab-s4-stage6d-source-check.sh
python3 tools/test-stage6d-model.py
```

Source/model checks do not replace a native ARM64 build or hardware tests.
