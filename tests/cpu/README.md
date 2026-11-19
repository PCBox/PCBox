# CPU microbenchmarks

`cpu_microbench` measures the current **x86-64 new dynarec** with its real IR
compiler, register allocator, instruction emitters and memory-helper ABI. It
does not boot a VM, need ROMs, or access guest disks. The executable has no
Google Test or Google Benchmark dependency.

## Build and run

Use an optimized x86-64 GCC or Clang configuration. The target is available
in a normal emulator build with both `BUILD_TESTING` and `BUILD_BENCHMARKS` off.
Run these commands from the repository root on Linux.

```sh
cmake -S . -B build/jit -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build/jit --target cpu_microbench
build/jit/tests/cpu/cpu_microbench --quick
build/jit/tests/cpu/cpu_microbench --csv build/before.csv
```

With Windows/MinGW use the same CMake commands and run
`./build/jit/tests/cpu/cpu_microbench.exe`. In CLion select the
`cpu_microbench` target in an optimized CMake profile. The default full run
measures 379 cases with 21 samples per case, 75 ms per sample and 100 ms
of warmup. Use `--quick` for a correctness smoke run. With `BUILD_TESTING=ON`,
CTest also runs execution and compile-mode smoke tests.

## Compare changes

Save a baseline, change/build the code, then run:

```powershell
& ./build/jit/tests/cpu/cpu_microbench.exe --baseline build/before.csv --csv build/after.csv
```

The baseline and output paths are relative to the working directory.
A delta of `+7%` means nanoseconds per operation increased by 7%. Missing
baseline cases show `--`. Different block sizes are rejected. Keep host, build flags and sample
settings the same. Repeat the baseline and candidate if the difference is small;
turbo, thermal changes, scheduling and other applications can move the results.

For a reproducible branch comparison on Windows/MinGW, use the paired runner:

```powershell
./tests/cpu/build_microbench_compare.ps1 -BaselineRef HEAD -OutputDirectory build/comparison -Compiler C:/msys64/mingw64/bin/gcc.exe
python tests/cpu/compare_microbench.py --baseline build/comparison/baseline.exe --current build/comparison/current.exe --output build/comparison/results --cpu 4
```

The build script exports the baseline revision into the output directory and
copies the current benchmark fixture and its support file into that snapshot. Both builds use the same compiler and optimization flags.
`build.json` records revisions, source changes, flags and hashes. Choose a logical
CPU available to your process. The runner also accepts Linux benchmark binaries
and pins them with the same `--cpu` option.

The runner randomizes case order with a recorded seed and uses paired AB/BA
order across two rounds. Defaults are 11 samples of approximately 75 ms per
round, giving 22 samples and about 1.65 seconds of measured work per case per
version. Allow roughly 20–30 minutes. Run only one comparison at a time. Avoid
debuggers and other heavy work while timing. Longer samples reduce timer and
scheduler noise, but do not guarantee lower variance.

Outputs include `comparison.csv`, pooled `baseline.csv` and `current.csv`, a
long-form `samples.csv`, the run manifest, and each individual CSV/log under
`runs/`. Pooled CSVs are for analysis; use individual harness CSVs with the C
executable's `--baseline` option. The runner independently checks the reported
medians and standard deviations against the raw observations. No samples are
discarded. Use `--resume` with unchanged settings and executables to continue an
interrupted run; completed, validated runs are retained.

The comparison labels a gain/regression only when it exceeds 3%, has the same
direction by at least 3% in every round, exceeds three combined relative median
absolute deviations, and both pooled coefficients of variation are at most 5%.
These are screening rules, not statistical significance tests. Cases with CV
above 5% are labeled `noisy`; smaller or inconsistent differences are
`no_clear_change`. Check raw samples and repeat important findings. Samples
within a process are correlated, and two process rounds cannot establish a
precise confidence interval or attribute a result to a particular commit.

Use `--filter substring` or `--case-file names.txt` with the runner for follow-up
measurements. `--block-ops 1 8 32 64` covers multiple block sizes. JMP cases encode
their own body lengths in their names and keep those lengths across this sweep.

Useful arguments:

```text
--list                                      list cases without running
--filter ram/load32                          focus on 32-bit RAM loads
--filter sse/ --sample-ms 100 --samples 15    longer SSE measurements
--block-ops 1                               expose block entry/exit overhead
--block-ops 64                              amortize that overhead over more work
--quick                                    3 short samples; validation/smoke run
--case loop/jmp8/integer/body-1              one exact case (no substring matches)
--cpu 4                                    pin the process to logical CPU 4 (Windows/Linux)
--warmup-ms 250                             extra untimed warmup per case
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
| `pressure` | Six live GPRs, seven live SIMD registers, or both, around aligned accesses, page splits and lookup misses |
| `alignment` | Intermediate byte offsets for 32/64/128-bit loads and stores |
| `x87` | Single/double conversion loads and stores with dynamic TOP, on inline and helper paths |
| `stream/.../stride-*` | Contiguous and 4 KiB strides through 32 KiB, 1 MiB and 16 MiB working sets |
| Additional arithmetic | Integer XOR, IMUL and shifts, scalar ADDSS, and SHUFPS at different register pressures |
| `loop` | Actual rel8/16/32 JMP frontend and unrolling gate, with integer/SIMD bodies from 1 to 32 operations |
| `string/movs*` | Non-REP byte/word/dword MOVS translators, a16/a32, both directions, with inline RAM or synthetic memory helpers |

Arithmetic cases measure backend operations; they do not include opcode decoding
or the complete guest-instruction frontend. Memory cases compile repeated IR
loads/stores, keeping each load required. Stream steps include address increment
and wraparound. `cycles-live` adds two guest-cycle updates per block. Arithmetic
register allocation/spills and block entry/exit are part of the measurements.
MOVS cases count one complete copy instruction as one operation, including its
index updates. Each block resets ESI/EDI before a burst in two fixed RAM pages;
that setup is included in timing. For long bursts the register allocator can
end the block early. `body_ops` records the number of MOVS instructions
compiled and execution times use that count. Segments are valid, and REP is not measured.
Pressure cases keep modified values live across the accesses and verify their
preservation. Their setup/writeback cost is included and amortized per memory
access. SSE check cases also cover joins and helper calls with live SIMD values.

JMP cases use synthetic instruction metadata and bytes with the production JMP
translator, unrolling gate, allocator and IR duplication. Context restoration
matches `codegen_set_loop_start` for the fixture's decoding context. One operation
is one arithmetic body operation plus its guest-cycle deduction. `body_ops` and
`unroll_copies` distinguish the loop length from the actual work in each generated
block; `ops_per_block` includes all copies. Thus a baseline that does not unroll
and a candidate that does are normalized to equal work. Validation checks the
result, PC and deducted cycles for that actual work. These cases include host
call/return overhead, but not the emulator's real dispatcher or event handling.

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
It is not the p95 latency of an individual memory access. CSV also contains mean,
sample standard deviation, coefficient of variation (CV), median absolute
deviation (MAD), maximum, total measured milliseconds, every sample, iteration
counts, validation helper calls per block and allocated JIT payload bytes.
Metadata records host CPU, compiler/build and register-pool sizes.

Slow-path callbacks access synthetic RAM and reproduce the fixture's PIII
misalignment penalty. They **do not** measure the real MMU page walker, devices,
code invalidation or a guest OS. `helpers/block` is counted during validation;
counter increments are disabled during timing. These cases isolate the emitted
fallback, register writeback/reload and C-call costs. They are not MMIO benchmarks.

The default execution mode excludes JIT compilation cost, dispatcher block lookup,
devices, rendering, interrupts and real instruction mixes. It cannot convert
ns/op into a sustainable Pentium III MHz value. Keep the SSE soak test as an
end-to-end check: a local microbenchmark win can still lose in a larger workload
through code size, cache behavior or a different instruction mix.

### Compilation measurements

Pass `--measure compile` to either the executable or paired runner to measure
**nanoseconds per compiled block**, independently of execution throughput:

```powershell
python tests/cpu/compare_microbench.py --baseline build/comparison/baseline.exe --current build/comparison/current.exe --output build/comparison/compile --measure compile --filter compile/ --block-ops 1 8 32 64 --cpu 4
```

This mode accepts the existing cases and adds three `compile/` cases. They vary
eight modified integer and eight SIMD registers across stores to exercise
differing allocator snapshots: fixed dword stores, alternating word/dword/qword
stores, and three phases of those widths. The phased case exposes repeated
searches through earlier sites with incompatible helper types. These three
cases also accept block sizes up to 192 operations; other cases remain limited
to 64. Each iteration constructs fresh IR, allocates registers,
emits code and resolves branches/stubs. It reuses the same executable arena;
shared helper creation, OS allocation, instruction-cache flushing, execution,
validation and state resets are outside timing. The first and last generated
blocks are executed and validated. This measures backend block construction,
not the complete guest decoder, dispatcher, cache eviction or an OS startup.
`ops_per_block` is 1 in compilation CSVs, `body_ops` records the compiled body
length, and `measure` identifies the units. Keep compilation and execution
results in separate directories. The executable's `--baseline` option is for
execution only; use the paired runner for compilation comparisons.
`slow_sites` and `slow_stubs` report the captured memory sites and distinct
helper stubs when the backend exposes those counters. A value of -1 means
the counter is unavailable.
