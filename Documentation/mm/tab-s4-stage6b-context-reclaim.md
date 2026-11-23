# Tab S4 Stage 6B: bounded context-sensitive reclamation

**Foundation:** `experiment/stage5-lmkd-vmstat-compat` at `88c15c303276dc043fc830a81948a9dbfe3e7eca`  
**Implementation branch:** `experiment/stage6b-context-reclaim`  
**Device:** Samsung Galaxy Tab S4 SM-T830 (WiFi), Android/LineageOS-derived 4.4 kernel

## Scope and motivation

Stage 5 SimCity captures demonstrated complete logical zRAM exhaustion,
heavy file-cache refault activity, allocstalls, high `kswapd` CPU usage, and
kernel OOM kills. Stage 5's aggregated `/proc/vmstat` fields for modern
Android `lmkd` are preserved. Stage 6B experiments with *bounded* short- and
long-term workload activity scores to reduce **premature** reclaim of a
recently refaulting **class** of pages; it cannot create additional memory.

This implementation is **class-level** rather than per-process virtual-address
region or page-level: the context consists of an existing (zone, memory-cgroup
LRU vector, anonymous/file) combination. This is deliberate for an initial
4.4 backport: no new page metadata, reverse mappings or per-access hooks.

## Data model

When `CONFIG_TAB_S4_STAGE6B=y`, `struct zone_reclaim_stat` gains, per
anonymous/file class:

- `fast[2]`: saturating unsigned 0..15, ages by 1 every second;
- `slow[2]`: saturating unsigned 0..15, ages by 1 every 8 seconds;
- 64-bit ARM64 jiffies timestamps for each score's last decay.

Changes to score fields are serialized by the **existing** `zone->lru_lock`.
The zero-initialized data are allocated as part of each existing `lruvec`,
so there is no per-page allocation and no auxiliary eviction cache.

### Existing event paths used

1. `workingset_note_file_refault()`: accepted file refault raises fast by
   up to 3 and slow by up to 1; `workingset_note_anon_refault()` makes the
   same update for accepted swap-cache anonymous refault.
2. `shrink_active_list()`: when at least 16 active pages were scanned and
   at least half are referenced/rotated, fast is incremented up to 15.
3. `shrink_inactive_list()`: a productive scan of at least 16 pages with
   >=75% reclaimed decrements fast once, never below 0.
4. `get_scan_count()`: reads and lazily ages both scores. Under normal
   global reclaim, reduces each class's *calculated* scan target by
   `min(25%, fast + slow)`. Always executes at least 75% of the selected
   scan count and retains all original LRU and refault decisions.

These signals are intentionally coarse and approximate. `protected_scan_targets`
counts reductions in planned scan iterations, **not** pages proven saved from
eviction. The scores do not identify which file or which heap object refaulted.

## Safety gates

Protection is **automatically disabled** when any of the following apply:

- The experiment is switched off at runtime.
- Reclaim is for a memory cgroup rather than global reclaim.
- The reclaim priority is <=3 (urgent).
- Stage 5 has entered the forced-deactivation retry.
- Anonymous swap is not permitted for the current reclaim.
- The zone requests memory boosting.
- Available swap slots <= max(8 * SWAP_CLUSTER_MAX, total_swap_pages / 16).
- On `get_scan_count()`'s second forced minimum-scan pass, there is no
  scan-target adjustment.

Thus normal reclaim and swap decisions remain active. The patch introduces
no permanent pins, no `PageUnevictable` flags, no additional page-access
tracking, no timers and no change to zRAM compression or swap size.

**Important tradeoff:** Once swap is depleted, 6B intentionally stops
protecting the file class; it cannot cure the catastrophic Stage 5
swap-exhaustion loop by withholding every available file page. The earlier
LMKD compatibility work remains necessary.

## Runtime A/B test

The feature is compiled in the experimental WiFi defconfig and defaults to
*enabled*, but it is switchable without rebuilding:

```sh
su -c 'echo 0 > /proc/sys/vm/tab_s4_stage6b_protection'  # Shadow-only baseline
su -c 'echo 1 > /proc/sys/vm/tab_s4_stage6b_protection'  # Bounded protection
su -c 'cat /proc/tab_s4_stage6b'
```

Scores continue to update with mode 0; only the reduction of scan targets
is disabled. For clean comparable observations, reboot between test
conditions, start SimCity from the same initial state, and use the same
background app set and `lmkd` properties. Compare at least:

- Stage 5 aggregate `pgscan_direct`, `pgscan_kswapd`, `pgrefill`;
  per-zone `pgsteal_direct_*` and `pgsteal_kswapd_*` (the Stage 5 patch
  does not provide aggregate `pgsteal` aliases),
- `allocstall`, file/anon workingset refault and activation rates;
- `pswpin`, `pswpout`, zRAM usage and free logical swap slots;
- PSI `some/full` and any ANR/OOM/LMKD kill events;
- `/proc/tab_s4_stage6b` deltas in both modes.

If the device becomes slower or freezes earlier in mode 1, revert to mode 0.
This is an experiment, not an established performance improvement.

## Build, packaging and verification

Use the companion Stage 6B build/repackaging ZIP, which contains WSL/Linux
scripts and a test capture harness. Validate with:

```sh
bash tools/tab-s4-stage5-source-check.sh
bash tools/tab-s4-stage6b-source-check.sh
```

Build with a compatible AArch64 Android GCC toolchain. The experimental
`gts4lwifi_defconfig` must retain all Stage 5 features plus
`CONFIG_TAB_S4_STAGE6B=y`, `CONFIG_TAB_S4_LMKD_VMSTAT_COMPAT=y`,
`CONFIG_ZSWAP=n`, and disabled `DIRECT_RECLAIM_FILE_PAGES_ONLY`.
Inspect the generated **and embedded** configuration before boot testing.

A known-working original WiFi boot image from the exact installed ROM is
required for repackaging. Preserve ramdisk, DTB, vendor headers and signing.
The tooling never flashes automatically.

## Known gaps and validation status

- GitHub branch contains source code and source-level anchors; a native
  ARM64 kernel build and SM-T830 boot/SimCity run have **not** been performed
  by the authoring environment.
- This implementation does not reconstruct address-level associations,
  track actual page reuse intervals or implement a replacement LRU.
- A/B mode 0 should be semantically equivalent to Stage 5 for LRU scan targets,
  but the counters introduce overhead. Verify parity on hardware.
- `CONFIG_TAB_S4_STAGE6B=n` eliminates the score fields and policy hooks,
  retaining the Stage 5 reference.
- Reboot resets all scores and counters. The sysctl defaults to 1 on boot.
- Only the first scan pass can receive bounded protection; emergency/forced
  scans and Stage 5 LMKD/OOM behavior are deliberately unchanged.
