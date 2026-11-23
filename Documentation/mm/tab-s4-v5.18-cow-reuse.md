# Tab S4 Linux 4.4.302: Linux 5.18 anonymous COW reuse experiment

## Scope and origin
Branch: `experiment/v5.18-cow-reuse-stage5`. Based on the modified Stage 5 branch; Stage 5 itself is unchanged. The contemporary Stage 5 source already contained a dormant `CONFIG_TAB_S4_V518_COW_REUSE` implementation in `mm/memory.c`, but its WiFi defconfig and `mm/Kconfig` did not expose or enable that symbol. This branch consolidates the dormant code, adds a conservative locked mapcount check, defines its Kconfig option and enables it for WiFi.
Upstream references:
- 53a05ad9f21d858d24f76d12b3e990405f2036d1: optimize do_wp_page() for exclusive pages in swapcache.
- d4c470970d45c863fafc757521a82be2f80b1232: optimize do_wp_page() for fresh pages in local LRU pagevecs.
- Linux 6.18 folio/PageAnonExclusive ownership is deliberately NOT imported. It relies on wider PTE/fork/GUP and swap entry invariants.

## Implemented
Under CONFIG_TAB_S4_V518_COW_REUSE, Stage 5 `mm/memory.c:do_wp_page()` now:
1. Rejects KSM pages, and early-rejects refcounts > 3.
2. Conditionally calls `lru_add_drain()` on pages not yet marked PageLRU, to release local pagevec references.
3. Rejects if the page still has any references beyond its mapping and possible swapcache reference.
4. Acquires the page lock with trylock_page (falls back to old COW copy if unavailable).
5. Tries to remove the swapcache reference using the existing `try_to_free_swap()`; its implementation refuses if swap count remains, writeback is active, or storage is suspended.
6. **Retains the legacy under-lock checks** `!PageKsm`, `page_mapcount == 1`, `page_count == 1` before reusing. No extra page sharing is authorized.
7. Existing old logic remains selected when the config is disabled.

This specifically reduces avoidable *copy-on-write page allocation*, not anonymous memory created through legitimate writes to previously untouched pages.

## Explicit non-goals
- Do not import 5.18 PageAnonExclusive / GUP / swap-entry / fork rewrite without separate security review.
- Do not import 6.x folio-based COW ownership, large folios, or speculative optimization of do_swap_page.
- No change to zRAM/ZSTD, swap sizing, reclaim, Stage 3 hotfix state, watermark settings, or userspace LMKD.

## Validation requirements (not yet completed)
- Compile this branch on the existing WSL GCC toolchain; the original Stage 5 build kit pins a different commit and MUST NOT be bypassed. Clone/update a branch-specific build kit, updating its expected SHA and config validation.
- Check compiled and embedded `CONFIG_TAB_S4_V518_COW_REUSE=y`.
- Boot and run repeated fork/COW tests before any SimCity stress testing; test a shared forked anonymous page to ensure writes to the parent never alter a child's original contents and vice versa.
- Run read-fault -> swap-out -> read-fault -> write-fault using zRAM; compare COW fault copy frequency against base Stage 5.
- Run a multi-threaded write-fault stress test with concurrent swap pressure, and check dmesg for bad page/rmap warnings.
- Device validation must record the exact `git rev-parse HEAD` and flashed Image.gz hash. Record matched workload PSI, allocstall, pswpin/out, anon footprint, file cache and crash evidence.
- Native build, test, runtime and functional performance claims are NOT established by a GitHub source commit.

## Note about limited benefits
The optimization only helps when reference counts can be reduced safely. Pages genuinely shared after fork, GUP-pinned, written back, or referenced on remote CPU pagevecs still trigger the original copy fallback. Heavy swap thrashing may continue due to the legitimate anonymous working set.
