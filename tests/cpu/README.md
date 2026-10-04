# CPU microbenchmarks

`cpu_microbench` measures the current **x86-64 new dynarec** with its real IR
compiler, register allocator, instruction emitters and memory-helper ABI. It
does not boot a VM, need ROMs, or access guest disks. The executable has no
Google Test or Google Benchmark dependency.

## Run in CLion

1. Reload the CMake project after pulling the changes.
2. Select **CPU microbenchmarks** and **Run**, using the existing
   **MSYS2 RelWithDebInfo** CMake profile. If your profile has a different name,
   select it in the run configuration; the target is `cpu_microbench`.
3. Results appear in the console and in `build/cpu-microbench.csv`.

The shared configuration builds the target first and runs nine samples per
case, targeting at least 25 ms per sample. Allow roughly a minute for the full
suite. Each run replaces that CSV, so copy a result you want to keep before
running again. Use Run, without attaching a debugger, for timing comparisons.

The target is available without enabling `BUILD_TESTING` or `BUILD_BENCHMARKS`.
It is excluded from the default build. GNU/Clang on x86-64 are supported by the
fixture; Windows/MinGW is the validated configuration. Use the same optimized
CMake profile for both the emulator and the benchmark.

From the repository root with the MinGW tools on PATH:

```powershell
cmake --preset windows-mingw64
cmake --build build/windows-mingw64 --target cpu_microbench -j 4
& ./build/windows-mingw64/tests/cpu/cpu_microbench.exe --csv build/before.csv
```

## Compare changes

Save a baseline, change/build the code, then run:

```powershell
& ./build/windows-mingw64/tests/cpu/cpu_microbench.exe --baseline build/before.csv --csv build/after.csv
```

In CLion, set the program arguments to:

```text
--samples 9 --sample-ms 25 --baseline before.csv --csv after.csv
```

The configuration's working directory is `build`, so those filenames refer to
`build/before.csv` and `build/after.csv`. **Positive delta means slower**:
`+7%` means nanoseconds per operation increased by 7%. Missing baseline cases
show `--`. Different block sizes are rejected. Keep host, build flags and sample
settings the same. Repeat the baseline and candidate if the difference is small;
turbo, thermal changes, scheduling and other applications can move the results.

Useful arguments:

```text
--list                                      list cases without running
--filter ram/load32                          focus on 32-bit RAM loads
--filter sse/ --sample-ms 100 --samples 15    longer SSE measurements
--block-ops 1                               expose block entry/exit overhead
--block-ops 64                              amortize that overhead over more work
--quick                                    3 short samples; validation/smoke run
```

Filters are case-sensitive substrings. Options are applied left to right.
`--help` lists the numeric limits. No match or any failed validation exits with
a nonzero status. There are no machine-dependent performance pass/fail thresholds.

## What is measured

| Group | Work |
|---|---|
| `control` | Empty generated block, reported as ns/block |
| `ram` | 8/16/32/64/128-bit loads/stores; aligned, unaligned, cache-line split, last page-contained address, page split, missing lookup entry |
| `cycles-live` / `cycles-memory` | Cycle counter used by IR across memory accesses, versus no explicit cycle-counter IR; actual caching follows allocator pressure |
| `form` | Absolute and immediate scalar forms, plus x87 single/double memory conversion paths |
| `stream` | 32/128-bit accesses stepping 64 bytes through 32 KiB, 1 MiB and 16 MiB working sets |
| `integer`, `mmx`, `sse` | ADD, PADDD, ADDPS and MULPS; one dependency chain or several independent registers, including eight live SIMD values |
| `sse/entry-checks` | Consecutive checks eligible for coalescing, and checks separated by memory accesses |

Arithmetic cases measure backend operations; they do not include opcode decoding
or the complete guest-instruction frontend. Memory cases compile repeated IR
loads/stores, keeping each load required. Stream steps include address increment
and wraparound. `cycles-live` adds two guest-cycle updates per block. Arithmetic
register allocation/spills and block entry/exit are part of the measurements.

Each case is compiled and validated before timing. Validation checks results,
cycle accounting, streaming wraparound and unexpected exceptions. A calibration
pass warms the code/data and chooses a batch length. Samples time many calls to
the generated block with `QueryPerformanceCounter` (Windows) or
`CLOCK_MONOTONIC` (POSIX), then divide by the number of operations. The reported
fractions of a nanosecond are **amortized throughput**, not individual-instruction
stopwatch readings or instruction latency. Timer calls, state resets, compilation,
validation and console output are outside the timed batch. Host loop/call costs
remain included; the empty-block result is provided for context, not subtracted.
One packed SIMD operation counts as one operation, not one per lane. The coalesced
entry-check case divides by the requested checks even though only the first check
is emitted; its small ns/op reflects that optimization.

The table shows median, minimum and nearest-rank p95 across batch averages.
With nine samples p95 is the largest sample; it is not the p95 latency of an
individual memory access. CSV contains those summaries, every sample, iteration
counts, validation helper calls per block and allocated JIT payload bytes.
Metadata records host CPU, compiler/build and register-pool sizes.

Slow-path callbacks access synthetic RAM and reproduce the fixture's PIII
misalignment penalty. They **do not** measure the real MMU page walker, devices,
code invalidation or a guest OS. `helpers/block` is counted during validation;
counter increments are disabled during timing. These cases isolate the emitted
fallback, register writeback/reload and C-call costs. They are not MMIO benchmarks.

This suite deliberately excludes JIT compilation cost, dispatcher block lookup,
devices, rendering, interrupts and real instruction mixes. It cannot convert
ns/op into a sustainable Pentium III MHz value. Keep the SSE soak test as an
end-to-end check: a local microbenchmark win can still lose in a larger workload
through code size, cache behavior or a different instruction mix.
