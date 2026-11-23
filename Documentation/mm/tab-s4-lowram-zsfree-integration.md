# Tab S4 4.4.302 low-RAM integration: PSI/lmkd, targeted reclaim and zsmalloc

Integration branch: `experiment/lowram-zsfree-stage5`
Parent: `experiment/v5.18-cow-diagnostics-stage5`. Original Stage 5 and COW test branches are unchanged.

## Audit and conflict decisions

| Feature | Pre-existing on branch | Integrated policy |
| --- | --- | --- |
| Kernel PSI / Android user-space lmkd | `CONFIG_PSI=y`, memcg and swap accounting enabled; `CONFIG_ANDROID_LOW_MEMORY_KILLER=n` | Keep PSI lmkd, **do not enable legacy in-kernel LMK**; it can suppress/change modern lmkd and can perform expensive killing during reclaim |
| zRAM compressed swap | `CONFIG_ZRAM=y`, default ZSTD, ZSMALLOC, ZRAM_DEDUP compiled | Retain. Verify `/sys/block/zram0/comp_algorithm`, `/proc/swaps`, and `use_dedup` on device. Compile support does NOT imply dedup is initialized at boot |
| zRAM writeback | disabled | **Keep disabled**; no backing-store writes or associated flash-wear risk introduced |
| zswap | disabled after prior freezes | Retain disabled; do not duplicate zRAM compression |
| 5.9 anonymous workingset, reclaim retry, watermarks, 5.18 COW / COW diagnostics | already present | unchanged |
| Legacy in-kernel process reclaim | code present, `CONFIG_PROCESS_RECLAIM=n` | Fix obvious existing bugs and enable explicit `/proc/PID/reclaim` only; no automatic reclaim daemon or added kernel background workload |
| 7.3 zsmalloc `zs_free()` lock split | not present; no `pool->lock` lookup in our older implementation | Adapt only the class-lock release piece, gated by `CONFIG_TAB_S4_ZS_FREE_UNLOCKED=y` |

## Adaptation of upstream Linux 7.3 zs_free optimization

Reference: Linux 7.3 memory-management updates / Wenchao Hao and Xueyuan Chen 2026 series, in particular `mm/zsmalloc: drop class lock before freeing zspage`.

Prior implementation: `zs_free()` held `class->lock` through `free_zspage()` / `__free_zspage()`; those call `put_page()` while the spinlock is held and may contend with allocator locks. New implementation:
- With the size-class spinlock held and only on the `ZS_EMPTY` path, `trylock_zspage()` succeeds or deferred worker is kicked as before.
- On success, detach zspage from `ZS_EMPTY` list and update class+pool allocated statistics under the spinlock.
- After dropping the spinlock, invoke `__free_zspage_pages()`, which validates the empty zspage and releases locked constituent pages to the allocator, then frees metadata.
- Keep existing `__free_zspage()` call sites for compaction, deferred free and other users unchanged; existing normal behavior remains available by disabling `CONFIG_TAB_S4_ZS_FREE_UNLOCKED`.
- Do not import 7.3 class index encodings or pool lock removals: the former requires unrelated handle changes, and the latter is already effectively handled by the legacy size-class lookup.

This lock split is an experimental functional backport; native compile, lockdep, KASAN and device stress verification are still required.

## Process reclaim corrections

`fs/proc/task_mmu.c` legacy `reclaim_write()` and `reclaim_pte_range()` had several hazards:
- `~0` assigned to signed `nr_to_reclaim` produced -1 rather than an unlimited positive target. Use `INT_MAX`.
- `nr_scanned` wasn't initialized in `reclaim_write()`. Initialize all fields before reclaim.
- The address-range VMA walk's hugepage skip didn't advance `vma`. Advance before continuing and skip mlocked VMAs.
- The PTE walk passed `pte - 1` into `pte_unmap_unlock()`, unsafe when stopping at the first PTE. Retain the initial mapping pointer.
- A full batch would retry the same final address; increment to the next page when breaking.

The newly enabled `CONFIG_PROCESS_RECLAIM=y` only exposes `/proc/PID/reclaim` for authorized writers. It DOES NOT instruct lmkd to call it. Avoid `echo anon` or `echo all` into SimCity's PID while benchmarking; that would force additional swap traffic. Test file-only reclaim on a disposable background app first if testing manually.

## Remaining caveats / differences against Samsung factory software

- Android-side `lmkd` parameters and the actual runtime zRAM algorithm/dedup flag are not part of the kernel Git tree. Verify with ADB. No claim that settings have been changed by this commit.
- `CONFIG_INCREASE_MAXIMUM_SWAPPINESS`, `CONFIG_FIX_INACTIVE_RATIO`, `CONFIG_TIGHT_PGDAT_BALANCE` appear as =y in the WiFi defconfig, but `mm/Kconfig` makes them depend on ZSWAP, which is disabled. The compiled `.config` may omit them. Do not assume they are active. Run the new build's Kconfig verification.
- Stage 5 includes custom file cache trim and swap shadow logic. This integration does NOT rewrite those areas. Previous Stage 5 documentation referring to the removed Stage 3 Hotfix1 active-file guard is stale.
- Never enable both kernel LMK and PSI-based modern `lmkd` without deliberately changing Android userspace mode.
- Do not combine this branch with another simultaneous swap/zRAM/dedup timing experiment before initial stability baseline.

## Validation gates

1. Native arm64 build on the established WSL toolchain. Must verify Git HEAD pinned SHA and `CONFIG_PROCESS_RECLAIM=y`, `CONFIG_TAB_S4_ZS_FREE_UNLOCKED=y`, `CONFIG_TAB_S4_V518_COW_REUSE=y`, `CONFIG_TAB_S4_V518_COW_DIAGNOSTICS=y`, `CONFIG_PSI=y`, `CONFIG_MEMCG=y`, `CONFIG_ZRAM=y`. Must additionally check `# CONFIG_ANDROID_LOW_MEMORY_KILLER is not set`, `# CONFIG_ZRAM_WRITEBACK is not set`, `# CONFIG_ZSWAP is not set`.
2. Verify both `out/.config` and extracted embedded config, source artifact SHA and Windows copied kernel SHA. Preserve DTB, ramdisk, Magisk, boot metadata; flash via Odin only after repack verifies unchanged components. Keep last bootable image for rollback.
3. Boot and confirm `/proc/config.gz`, `/proc/tab_s4_cow`, `/proc/PID/reclaim` existence for disposable PID. Confirm `lmkd` runs with PSI support using Android logs and properties.
4. COW fork tests 100, then 1000; bounded swap COW tests; repeated SimCity city movement and app switch. Record zRAM mm_stat, PSI and vmstat, lmkd kills and any freezes.
5. Check memory pressure recovery during mass background app exit. The expected zsmalloc benefit is lower lock contention, not smaller anonymous memory allocations.

Native build and tablet validation are not claimed by this source commit.
