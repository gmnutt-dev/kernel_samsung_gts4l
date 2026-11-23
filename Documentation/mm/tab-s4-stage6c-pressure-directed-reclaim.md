# Stage 6C: pressure-directed, bounded proactive background reclaim

**Source baseline:** `experiment/stage6b-context-reclaim` at
`5c6c0ac38e6972217b804e98ea6bf8fd08e97b3d`

**Device:** Samsung Galaxy Tab S4 SM-T830 WiFi / 4.4 Android kernel

## Goal

Reduce some *future* direct-reclaim demand through small early background
reclaim passes, without sacrificing Stage 6B's context-sensitive protection,
changing low-memory-killer behavior, changing ZRAM capacity or overriding any
Stage 5 emergency reclaim policy.

**This is experimental and disabled by default**, because 6B has not yet
been boot-tested. Testing the 6B policy separately first is the recommended
sequence, even when both policies are compiled into the same kernel.

## Event source

The existing global `vmpressure_notifier` already measures reclaim
inefficiency and stall-weighted pressure when page allocations enter the
slow path. Stage 6C registers for that callback. This does **not** directly
subscribe to PSI trigger notifications; PSI remains external telemetry.

Only pressure in 30..79, with more than 50% of logical swap slots still
free, can schedule a request. Notifications are coalesced (one outstanding),
limited by a three-second cooldown and expire after two seconds.

The accepted event wakes the **existing kswapd** on the node. No new
reclaim thread, polling timer, per-page history or per-access tracing exists.

## Extra reclaim (opt-in only)

After regular `balance_pgdat()`, kswapd atomically claims the event.
The additional work has these safeguards:

1. More than 50% of logical swap space must still be available.
2. A candidate zone must already satisfy its **normal high watermark**,
   but be within a small extra band above it (32–128 pages).
3. Samsung memory-boost mode and unreclaimable/empty zones are skipped.
4. Inspect at most eight memory-cgroup LRU vectors per candidate zone.
5. Read Stage 6B's separate anonymous/file activity scores under the
   existing zone LRU lock. Only a relatively cool inactive list (activity
   score <=12 of at most 25) may be selected.
6. Request at most 64 pages by default, capped at 16–128 pages by sysctl
   per accepted event, over *all zones/memcgs combined*.
7. Call the kernel's existing `shrink_inactive_list()` reclaim path, which
   retains page isolation, writeback checks, accounting and putback.
   Never deactivate active pages or pin any page.
8. File candidates request clean-page eviction only; anonymous candidates
   permit ZRAM pageout **only when spare swap capacity is sufficient**.

Note: the cap applies to **requested scan pages**, not time spent in the
reclaim path and not a hard cap on every page examined by its internal
isolation implementation. Successful page reclamation can be less than
the request. The initial conservative values need device telemetry.

The Stage 6C extra pass does not use the Stage 6B scan target optimizer
directly; instead, it consults the Stage 6B context scores to choose
the cooler of the two inactive LRUs. Ordinary kswapd and direct reclaim
retain all Stage 5/6B code paths.

## Configuration and A/B

```sh
# 6C is OFF at boot, even with CONFIG_TAB_S4_STAGE6C=y.
su -c 'cat /proc/sys/vm/tab_s4_stage6c_enable'
su -c 'cat /proc/sys/vm/tab_s4_stage6c_budget'
su -c 'cat /proc/tab_s4_stage6c'

# Independently select 6B mode and 6C mode:
su -c 'echo 1 > /proc/sys/vm/tab_s4_stage6b_protection'
su -c 'echo 0 > /proc/sys/vm/tab_s4_stage6c_enable'   # 6B alone
su -c 'echo 1 > /proc/sys/vm/tab_s4_stage6c_enable'   # 6B plus 6C
su -c 'echo 64 > /proc/sys/vm/tab_s4_stage6c_budget'
su -c 'echo 0 > /proc/sys/vm/tab_s4_stage6c_enable'   # immediate off
```

Suggested conditions on the *same compiled 6C kernel* with a reboot
between captures:

| Condition | 6B protection | 6C enable | Purpose |
|---|---:|---:|---|
| A | 0 | 0 | Stage 5 reclaim-policy reference with instrumentation |
| B | 1 | 0 | Stage 6B alone, original priority |
| C | 1 | 1 | Stage 6B + bounded Stage 6C |

Do not combine these A/B changes with LMKD tuning, swap-size changes,
ZRAM compression changes or different background workloads. Particularly
record the **starting swap occupancy** before SimCity.

The read-only `/proc/tab_s4_stage6c` exposes event acceptance,
pressure/cooldown/swap rejections, per-zone eligibility skips, requested
file/anon scans and actually reclaimed pages. Combine this with
`/proc/tab_s4_stage6b`, `/proc/vmstat`, PSI, `/proc/meminfo`, ZRAM
mm_stat, swaps and Android event logs.

## Rollback and known limitations

- No native AArch64 compile or SM-T830 boot/SimCity validation has yet
  been completed in the authoring environment.
- `CONFIG_TAB_S4_STAGE6C=n` gives the exact Stage 6B source reclaim
  path. `tab_s4_stage6c_enable=0` disables all extra scans without reboot.
- A notifier may only arrive **after a failed allocation has begun**. This
  is *reactive early background reclaim*, not prediction of every allocation.
- A small amount of extra reclaim can worsen file refaults or swap CPU
  work on some workloads; that is precisely why it is gated and optional.
- Excessive memory demand, zero free swap slots and LMKD failure modes
  are not fixed by proactive reclaim. Keep the known-working boot backup.
- The global notification queue is device-oriented (SM-T830 single node);
  multi-node fairness is not a goal of this experimental backport.

## Validation

```sh
bash tools/tab-s4-stage5-source-check.sh
bash tools/tab-s4-stage6b-source-check.sh
bash tools/tab-s4-stage6c-source-check.sh
```

Follow with a native `Image.gz-dtb` / `Image.gz` build, exact generated
vs embedded config comparison and normal Magiskboot boot image repack checks.
**Never flash based only on static checks.**
