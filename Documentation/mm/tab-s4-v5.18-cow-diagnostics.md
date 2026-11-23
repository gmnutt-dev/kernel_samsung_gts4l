# Tab S4 COW diagnostics (Linux 4.4.302 / 5.18 adaptation)

Branch: `experiment/v5.18-cow-diagnostics-stage5`; base: `experiment/v5.18-cow-reuse-stage5`.

## Changes
- `CONFIG_TAB_S4_V518_COW_DIAGNOSTICS=y` adds read-only `/proc/tab_s4_cow`.
- Atomic counters increment only at COW events. No tracing, logging or memory allocation on the hot path.
- `cow_copy`: successfully installed replacement page in `wp_page_copy()` (zero-page copies included).
- `cow_zero_copy`: subset of copies created from shared zero page (not an unnecessary duplicate).
- `cow_reuse`: anonymous page reused through `do_wp_page()`.
- `cow_swap_attempt`: `try_to_free_swap()` attempted.
- `cow_swap_released`: that call successfully removed the swapcache entry.
- `cow_pagevec_drain`: local LRU drain invoked, **not proof** that it released a reference.
- `cow_copy_fallback`: the `do_wp_page()` copy path entered, possibly including attempts later canceled by PTE races.
Counters are global and monotonic from boot. Use deltas for analysis.

## Native build
The existing user-local COW build script is pinned to the earlier revision and must be updated to the exact diagnostic branch commit before running. Keep its revision check; add mandatory checks for:
```
CONFIG_TAB_S4_V518_COW_REUSE=y
CONFIG_TAB_S4_V518_COW_DIAGNOSTICS=y
CONFIG_TAB_S4_V59_ANON_WORKINGSET=y
```
Verify these in BOTH `out/.config` and `scripts/extract-ikconfig artifacts/Image.gz`, then verify the copied kernel payload SHA-256 matches the build output. Preserve the established DTB, Magisk ramdisk, boot header, Odin package checks and rollback image. No build or flash is claimed here.

## Device collection
```
adb shell "su -c 'cat /proc/tab_s4_cow'"
adb shell "su -c 'cat /proc/vmstat'"
adb shell "su -c 'cat /sys/block/zram0/mm_stat'"
```
Capture each at baseline, during matched SimCity gameplay, after app switching, and on recovery. The counters are system-wide; simultaneous app traffic confounds attribution.

## Regression
Compile the supplied `tools/testing/selftests/vm/tab_s4_cow_swap_test.c` in Termux with `clang -O2 -Wall -std=gnu11 ... -o cow_swap_test`. Start with `./cow_swap_test 3 32 64`; then repeat with `8 64 128` if stable.
The test forks private anonymous mappings and verifies parent/child write isolation while applying bounded memory pressure. It reports system-wide `pswpin/pswpout` changes. **Nonzero swap delta doesn't prove the tested mappings were swapped**. Zero delta means that swap-dependent optimization is not validated by this test.
Repeat existing `cow_test 1000` before full SimCity load.

## Further work
A direct per-page swapcache reuse test requires reliably triggering a swapcache-backed write fault on a uniquely mapped page, preferably with targeted fault tracing; counters merely quantify events system-wide. Do not interpret `cow_swap_attempt` as all valid reuse candidates or `cow_copy_fallback` as confirmed completed copies. The COW test cannot rule out concurrent GUP/fork races. Verify native build and stress tests before merging into Stage 5.
