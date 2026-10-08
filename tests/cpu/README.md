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

The shared configuration builds the target first and runs 21 samples per
case, targeting 75 ms per sample after 100 ms of warmup. The suite contains
4,357 execution cases (4,360 in compilation mode); allow several hours for a full
run at these settings. Each run replaces that CSV, so copy a result you want to keep before
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
--samples 21 --sample-ms 75 --baseline before.csv --csv after.csv
```

The configuration's working directory is `build`, so those filenames refer to
`build/before.csv` and `build/after.csv`. **Positive delta means slower**:
`+7%` means nanoseconds per operation increased by 7%. Missing baseline cases
show `--`. Different block sizes are rejected. Keep host, build flags and sample
settings the same. Repeat the baseline and candidate if the difference is small;
turbo, thermal changes, scheduling and other applications can move the results.

For a reproducible branch comparison on Windows/MinGW, use the paired runner:

```powershell
./tests/cpu/build_microbench_compare.ps1 -BaselineRef codex/cpu-microbench-baseline -OutputDirectory build/comparison -Compiler C:/msys64/mingw64/bin/gcc.exe
python tests/cpu/compare_microbench.py --baseline build/comparison/baseline.exe --current build/comparison/current.exe --output build/comparison/results --cpu 4
```

The build script exports the baseline revision into the output directory and
backports only the current C fixture into that snapshot. It builds both versions
with the same compiler and optimization flags, without changing either branch.
`build.json` records revisions, source changes, flags and hashes. Choose a logical
CPU that exists on your machine; `--cpu` uses Windows process affinity.
The fixture aligns its synthetic CPU state, memory callbacks and timing loops
equally in both builds. Timing loops are not inlined, so calibration and samples
use the same call boundary. Generated code still uses each backend's real layout.

The runner randomizes case order with a recorded seed and uses paired AB/BA
order across two rounds. Defaults are 11 samples of approximately 75 ms per
round, giving 22 samples and about 1.65 seconds of measured work per case per
version. Allow several hours for the expanded suite. Run only one comparison at a time. Avoid
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
--cpu 4                                    pin the process to logical CPU 4 (Windows)
--warmup-ms 250                             extra untimed warmup per case
```

Filters are case-sensitive substrings. Options are applied left to right.
`--help` lists the numeric limits. No match or any failed validation exits with
a nonzero status. There are no machine-dependent performance pass/fail thresholds.

## What is measured

The expanded groups cover:

- `conditions/`: all sixteen SETcc conditions and word/dword CMOV, following
  byte/word/dword CMP or ADD, known and unknown producer metadata, two input
  patterns. One operation is a producer/consumer pair.
- `carry/`: CMP/ADD followed by word/dword INC/DEC, including carry and signed
  boundary inputs. Validation checks the preserved carry independently.
- `cmov-memory/`: all conditions, word/dword operands, RAM, page splits and
  lookup misses. Before a translator supports the memory form, a small C
  fallback model performs the load and condition evaluation. Its count is
  recorded as `frontend_fallbacks`; it excludes interpreter decoding and
  instruction timing and is not a measurement of the complete interpreter.
- `multiply/` and `divide/`: signed/unsigned low-half multiplication with
  different dependency counts, and the production dword DIV/IDIV frontend
  with multiple dividend shapes and live SIMD values. Division includes input
  setup and quotient/remainder accumulators, keeping both results observable.
- `matrix/`: RAM widths from byte through 128-bit, alignment/cache-line/page
  boundaries, missing mappings, 0–6 live GPRs, 0–7 live SIMD registers, and
  accesses separated by joins or C calls. The call performs no device work;
  it isolates the cost of losing the current register allocation.
- `working-set/`: 4 KiB through 16 MiB, all five widths, contiguous, cache-line,
  page and page-plus-cache-line strides, with and without register pressure.
- `pointer-chase/`: dependent loads through deterministic sequential or
  shuffled cache-line rings, with one, two or four independent chains. This
  distinguishes load latency from streaming throughput and prefetching.

Every execution case can also be measured with `--measure compile`. The CSV
records pre-unrolling IR operations, explicit call operations, memory operations,
barriers and frontend fallbacks, alongside code bytes and deferred memory sites.
`call_uops` counts explicit IR calls, not calls emitted inside arithmetic or
memory operations. The paired runner retains these columns for both versions.

Use `--validate-only` to execute and check the entire selected matrix without
timing it. It cannot be combined with CSV or baseline output. Useful sweeps:

```powershell
./build/windows-mingw64/tests/cpu/cpu_microbench.exe --validate-only --block-ops 1
./build/windows-mingw64/tests/cpu/cpu_microbench.exe --validate-only --block-ops 64
./build/windows-mingw64/tests/cpu/cpu_microbench.exe --filter pointer-chase/ --csv build/chase.csv
./build/windows-mingw64/tests/cpu/cpu_microbench.exe --measure compile --filter conditions/ --csv build/conditions-compile.csv
```

Block-size sweeps expose entry/exit amortization; joins and calls expose state
writeback; memory matrices separate register spills, lookup helpers and host
cache behavior. These are diagnostics for larger changes, not measurements of
the production dispatcher, real MMU walks, REP execution, devices or a guest OS.

| Group | Work |
|---|---|
| `control` | Empty generated block, reported as ns/block |
| `frontend` | CMP/ADD followed by ADC, SBB, SETB, SETBE or CMOVB, using the production lazy-flag translators |
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

Arithmetic cases outside `frontend/` measure backend operations; they do not include opcode decoding
or the complete guest-instruction frontend. The `frontend/` cases count one
producer/consumer instruction pair per operation, including flag bookkeeping
and any fallback C calls. They use synthetic decoder inputs and reproduce the
interpreter's translation-time flag metadata. Long frontend bursts honor the
allocator's block-end request before register versions overflow; `ops_per_block`
records the actual pairs, while `body_ops` records the requested burst length.
Memory cases compile repeated IR
loads/stores, keeping each load required. Stream steps include address increment
and wraparound. `cycles-live` adds two guest-cycle updates per block. Arithmetic
register allocation/spills and block entry/exit are part of the measurements.
MOVS cases count one complete copy instruction as one operation, including its
index updates. Each block resets ESI/EDI before a burst in two fixed RAM pages;
that setup is included in timing. Segments are valid, and REP is not measured.
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
code invalidation or a guest OS. `helpers/block` counts memory callbacks during validation
(not flag helpers);
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
`ops_per_block` is 1 in compilation CSVs, `body_ops` retains the requested block
length, and `measure` identifies the units. Keep compilation and execution
results in separate directories. The executable's `--baseline` option is for
execution only; use the paired runner for compilation comparisons.
`slow_sites` and `slow_stubs` report the captured memory sites and distinct
helper stubs (or -1 for revisions predating deferred stubs).

## CPU correctness tests without GUI dependencies

The CPU fixtures also form a standalone CMake project. This avoids the full
application's Google Test dependency and runs the same targets registered by
the main project when `BUILD_TESTING` is enabled:

```powershell
cmake -S tests/cpu -B build/cpu-tests -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=C:/msys64/mingw64/bin/gcc.exe
cmake --build build/cpu-tests -j 4
ctest --test-dir build/cpu-tests --output-on-failure
```

`cpu_fastpath_test` shares the microbenchmark's real JIT and synthetic RAM,
with independent arithmetic expectations. It covers exhaustive 8-bit carry
inputs; word/dword boundaries and seeded random inputs; known and unknown lazy
flags; inverted conditions; ADC/SBB byte, high-byte, word, dword, memory and
mutable-immediate forms; and fault ordering before memory writes commit.
Conditions are also tested at the allocator's register-version limit.
The x64 native unsigned-compare operation is exercised with every allocator
register and source/destination alias combination, including REX byte encodings.
RET tests cover both stack sizes, 16-bit SP wrapping, unsigned imm16 adjustment,
page-split instructions/stacks, mutable immediates, stack-limit faults and
memory aborts.

On Windows, generated callers seed and check all ten nonvolatile XMM registers
around blocks with 0–16 live SIMD values. These exercise spills, read-only
sources, joins, helper calls, early exits, dirty load destinations and allocator
chunk crossings. The mixed MMX/SSE fault tests also cover host register masks
beyond bit 15. Existing RAM/SSE tests remain separate targets and include the
production exception dispatcher and many memory-alignment/fault combinations.

The former profiling test registrations referenced `codegen_profile*.c` sources
absent from this checkout and have been removed.

These are execution and ABI tests, not a game compatibility test or a proof of
a sustainable guest clock rate. Run a representative game/VM for that last check.

## Fast-path measurements, 2026-10-06

Baseline: `098866151804a32b03d3eae566554750e72c9168`. Candidate: the working
tree with the RET imm16/operand32 table entry, selective Win64 XMM preservation,
native unsigned comparisons for known carry conditions, and the 32-bit memory
register-mask correction. The latter fixes preservation of upper SIMD slots on
helper/fault paths; the new execution tests exposed the old truncation.

Windows x64, Ryzen 9 9950X, logical CPU 4, MSYS2 GCC 15.2.0, identical `-O2`
builds with the current fixture backported to the baseline. The full 385-case
execution sweep used two counterbalanced rounds, seven 20 ms samples per round,
30 ms warmup and 32 requested operations per block. It classified 158 gains,
167 cases with no clear change, 53 noisy cases and seven regressions. Every
selected block passed result validation. These classifications are the screening
rule described above, not a guarantee about game performance.

The longer check used eleven 50 ms samples per round and 100 ms warmup. Its
medians for the affected fast paths were:

| Case | Before | After | Time change |
|---|---:|---:|---:|
| Empty block (ns/block) | 2.845 | 1.935 | -32.0% |
| CMP32 / ADC32 (ns/pair) | 1.196 | 0.363 | -69.7% |
| CMP32 / SBB32 (ns/pair) | 1.201 | 0.371 | -69.1% |
| ADD32 / ADC32 (ns/pair) | 1.226 | 0.378 | -69.1% |
| CMP32 / SETB (ns/pair) | 1.132 | 0.223 | -80.3% |
| CMP32 / SETBE (ns/pair) | 1.730 | 0.222 | -87.2% |
| CMP32 / CMOVB32 (ns/pair) | 1.180 | 0.239 | -79.8% |

**This candidate does not pass a strict no-performance-regressions gate.**
The longer check confirmed these six slower cases:

| Case | Before (ns/op) | After (ns/op) | Time change |
|---|---:|---:|---:|
| `ram/load32/page-split/cycles-memory` | 1.943 | 2.150 | +10.6% |
| `ram/load32/page-split/cycles-live` | 2.012 | 2.240 | +11.3% |
| `pressure/load32/page-split/simd-7` | 2.305 | 2.461 | +6.8% |
| `pressure/load64/page-split/simd-7` | 2.303 | 2.444 | +6.1% |
| `sse/entry-checks/store/lookup-miss` | 7.150 | 7.575 | +5.9% |
| `loop/jmp32/sse/body-1` | 0.412 | 0.512 | +24.1% |

The last case has ten unrolled body operations: its increase is about 1 ns per
generated block. The rel8/rel16 versions varied enough to be classified noisy
in the longer check; other runs also showed slowdowns. Code/body alignment and
allocator-order experiments did not reliably remove that result and were not
retained. These cases need further investigation before claiming regression-free
performance. No game/VM throughput measurement was made.

Additional runs covered 19 representative execution cases with one and 64
requested operations, and 15 compilation cases with one and 32 operations.
All validated. The native comparison keeps carry lowering to one IR operation;
32-pair flag-block compilation medians fell by 26–37% (five of six cases were
classified gains, one noisy). Of four initially flagged compilation slowdowns,
the longer check confirmed one: a single CMP32/CMOVB32 pair, 411 to 430 ns/block
(+4.7%). The other three did not meet the repeatability threshold on that run.

The RET translator's stack, immediate and fault behavior is covered by the new
tests. This fixture does not time the real decoder's removed interpreter fallback,
so no isolated RET speedup is claimed.

The full PCBox build and all eight standalone CPU suites passed. The new
`cpu_fastpath_test` completed 648,640 executions, including the native compare
alias checks; the existing suites also cover RAM/SSE preservation and decoding.

Local artifacts are under `build/cpu-throughput-fixes/`:

- `native-build/build.json`: compiler arguments, source diff, hashes and baseline snapshot.
- `native-execution/`: full sweep, raw samples and per-run validation logs.
- `native-execution-recheck/`: longer execution check, including all initially flagged cases.
- `native-block-sizes/`: one/64-operation execution comparisons.
- `native-compilation/` and `native-compilation-recheck/`: compilation measurements.
- `application-build.log` and `tests/Testing/Temporary/LastTest.log`: build/test evidence.

Reproduce the full sweep from the repository root, choosing fresh output folders:

```powershell
./tests/cpu/build_microbench_compare.ps1 -BaselineRef 098866151804a32b03d3eae566554750e72c9168 -OutputDirectory build/fastpath-compare -Compiler C:/msys64/mingw64/bin/gcc.exe
python tests/cpu/compare_microbench.py --baseline build/fastpath-compare/baseline.exe --current build/fastpath-compare/current.exe --output build/fastpath-compare/execution --samples 7 --sample-ms 20 --warmup-ms 30 --rounds 2 --block-ops 32 --cpu 4
```

Use the recorded case lists (`native-recheck-cases.txt`,
`representative-execution.txt`, `representative-compilation.txt` and
`native-compile-recheck-cases.txt`) with `--case-file` to reproduce the focused
runs. Their manifests record all other arguments. Earlier experimental binaries
and results in this local build directory are superseded by the `native-*` runs.

## Expanded matrix and arithmetic changes, 2026-10-07

The suite grew from 385 to 4,357 execution cases and 4,360 compilation cases.
The five changes were compared individually against their immediate parent:
native signed dword conditions, inline INC/DEC carry preservation, memory-source
CMOV, low-half UMUL through IMUL, and combined DIV/IDIV helpers. The host was
Windows x64 on the Ryzen 9 9950X, logical CPU 4, with MSYS2 GCC 15.2.0 and
identical `-O2 -march=x86-64` paired builds. Each pair used the same current
fixture and case definitions.

Focused execution comparisons included the affected cases and 22 unchanged
controls covering scalar/SIMD work, RAM helpers, register pressure, loops and
pointer chasing. Each used two counterbalanced rounds, seven 20 ms samples
per round, 30 ms warmup and 32 operations per block. Compilation comparisons
used the same settings with one and 32 operations per block. Examples classified
as gains, with execution time in ns per benchmark operation:

| Change and case | Before | After | Time change |
|---|---:|---:|---:|
| Signed conditions: CMP32 / SETL, input 1 | 2.083 | 0.221 | -89.4% |
| Carry preservation: CMP32 / INC, input 0 | 1.731 | 0.370 | -78.6% |
| Memory CMOVB32: RAM, input 0 | 4.389 | 1.721 | -60.8% |
| Low-half unsigned32 multiply, four live values | 0.304 | 0.136 | -55.3% |
| Unsigned DIV32, input 0, no live SIMD | 6.336 | 2.060 | -67.5% |

These isolated improvements do not predict a whole-VM speedup. CMOV's baseline
is the small fallback model described above: it omits interpreter decoding,
segment checks and instruction timing. The compiled path checks the segment
and performs an unconditional checked load. Its page-split CMOVE cases were
8–13% slower than the model, and CMOVP page-split/miss cases were 16–29% slower
in the longer check (eleven 50 ms samples, 100 ms warmup, two rounds). The other
four changes had no classified execution regressions in their focused screens.

Compilation has real tradeoffs. A single CMP32/SETL pair took about 20 ns more
(5%), while 32-pair blocks compiled 34% faster. Inline carry adds IR: the
CMP32/INC example rose from 6.73 to 7.24 microseconds per 32-pair block (7.5%);
word cases increased more, around 29–54% in the initial screen. Its empty-block
control also rose about 15%, so short-block changes cannot be attributed solely
to the extra IR. CMOVB32 compiled in 18.58 rather than 4.78 microseconds for 32
pairs, comparing a full translation with a call to the small fallback model.
Unsigned multiply compilation improved about 10–16% for 32-operation blocks;
the affected DIV/IDIV compilation cases improved about 11–25% across both sizes.
The division change also showed an unrelated 32-operation `compile/mixed-stores`
increase of 3.7%, repeated at 3.3% with eleven 60 ms samples. This series does
not establish a strict absence of performance regressions.

Every step passed all eight standalone CPU suites. The final fast-path suite
ran 1,755,888 executions, including signed boundaries, carry preservation,
memory faults on false CMOV conditions, multiply register aliases, and byte,
word and dword division results, overflow, zero divisors and CPU-specific flag
handling. The full matrix also validated with one and 64 operations per block.
The full Windows application build passed. Win64 was exercised; other host ABIs
and game/VM throughput were not measured in this series.

Final quick sweeps completed all 4,357 execution and 4,360 compilation cases.
The bulk execution sweep showed large RAM timing changes that did not reproduce
in fresh paired runs: nine selected cases had no clear change and one remained
noisy. For example, the same Release binary's store16/aligned/GPR4/join case
measured 3.90 ns/op in the bulk sweep and 0.39 ns/op in isolation. The cause of
this run-history sensitivity is unresolved. Treat bulk timings as exploratory;
use the paired runner's fresh process per case for performance comparisons.

Local evidence is under `build/cpu-expansion/`. Each of `signed-aligned/`,
`carry/`, `cmov/`, `multiply/` and `divide/` contains the paired binaries,
`build.json`, raw runs and `execution/comparison.csv` and
`compilation/comparison.csv`. `carry/recheck/` and `cmov/recheck/` contain longer
follow-ups. The case lists and per-step CTest logs are in the parent directory.
`divide/compile-recheck/` records the compilation follow-up;
`series/ram-recheck/` compares the complete series against `d1e33c54e` with the
same fixture. `final-execute.csv` and `final-compile.csv` are the quick sweeps,
and `application-build.log` records the application build. Use the comparison
commands above with the saved case lists to repeat a screen.

## Chunked REP MOVS/STOS, 2026-10-07

The dynarec REP handlers now copy/fill cached, untracked RAM in chunks of at
most 256 elements. Chunks stop at either operand's page, segment or address-size
boundary and retain the existing strict cycle-budget exit. Byte, word and dword
operations support both directions and both address sizes. Overlapping host
ranges (including aliased guest pages) use ordered element copies, preserving
REP's propagation behavior. MMIO, code-tracked writes, unaligned accesses,
watchpoints, traps and translation misses use the existing scalar accesses.
Two failed probes disable further probes for that invocation; a successful
chunk permits retries at the next boundary. Counts below eight stay scalar.

`rep_chunk_test` executes the production handlers and checks memory against an
independent element-by-element oracle. Its 1,556 cases cover register/flag
preservation, both cycle budgets, cold lookup charges, repeated restarts,
segment overrides and limits, address wrapping, overlap/aliases, MMIO and
tracked callbacks, traps/watchpoints, and partial progress before faults.
The same assertions pass against the original handlers. All nine standalone
CPU suites pass. The REP suite also passes with the old dynarec definitions,
with `USE_GDBSTUB` (chunking disabled), and with GCC's undefined-behavior
sanitizer in trap mode.

The fixture's optional `--bench` and `--bench-fallback` modes time complete REP
operations, including any cycle-budget restarts. A local Windows x64 comparison
used GCC 15.2.0, `-O3 -fno-strict-aliasing`, the default x86-64 ISA target,
logical CPU 4, 100 ms per case, and baseline/current/current/baseline order.
For 256-element cached RAM operations, MOVS was about 26–57 times faster and
STOS about 12–17 times faster across widths/directions. Overlap cases also
improved. This is a handler microbenchmark, not a whole-VM speedup. Single-element
cases were up to about 7% slower and the minimal MMIO MOVSB fixture up to about
11% slower; no universal absence of regressions is claimed.

Local evidence is in `build/rep-chunks/`: `comparison.json` records the baseline
revision, compiler, executable hashes and per-case results; `final-*.csv` holds
the paired raw measurements. `baseline-include/x86_ops_rep_dyn.h` preserves the
original handlers for reproducing the comparison with the same fixture.

## Paired 128-bit memory helpers (2026-10-07)

Compared against `b5cd05df4`, the paired helper shares compatible slow paths
and uses the existing register snapshot instead of saving volatile GPRs again.
Private sites keep the smaller pair of quad-helper calls. Memory-site records
shrank from 192 to 120 bytes, saving 288 KiB of fixed compiler scratch storage.

Windows x64, Ryzen 9 9950X, CPU 4, GCC 15.2.0, identical `-O2 -march=x86-64`
builds and fixtures. The screen covered 134 cases with two counterbalanced
rounds, seven 20 ms samples and 30 ms warmup. Compilation used one and 32
operations per block. Longer repeats used eleven 60 ms samples and 100 ms
warmup. Representative 32-operation compilation results from those repeats:

| Case | Before (us/block) | After (us/block) | JIT bytes before / after |
|---|---:|---:|---:|
| Aligned load128 | 4.31 | 2.81 | 4344 / 2665 |
| Aligned store128 | 4.08 | 2.52 | 4349 / 2612 |
| Load128 miss, mixed register pressure | 11.35 | 4.96 | 9876 / 3353 |
| Store128 miss, mixed register pressure | 11.31 | 4.47 | 9878 / 3135 |

High-pressure miss execution improved from 6.66 to 4.81 ns/op for loads and
6.82 to 4.44 ns/op for stores. Aligned RAM throughput was unchanged. One
low-pressure store-miss matrix case remained 4% slower (3.99 to 4.15 ns/op).
None of the compilation slowdowns flagged by the short screen reproduced as
a classified regression in the longer repeats. Single-operation private pairs
use four or five additional bytes; the large savings require shared sites.
The scalar mixed-store compilation controls improved roughly 4-8%, including
192-operation blocks, without changing their emitted byte counts.

A separate paired run used `1da8cbe9b`, before all performance additions, with
the same fixture. Aligned load/store compilation remains 47%/41% slower than
that original baseline, and the two mixed-pressure cases remain 71%/64% slower.
These changes reduce the earlier regressions; they do not eliminate them.

All nine standalone suites pass, including 40,151 RAM/register cases covering
faults on either half, callback mapping changes, partial stores, address wrap,
register pressure and allocator boundaries. All 4,357 benchmark cases validate
at one, 32 and 64 operations. The full Windows application builds. Other host
ABIs and whole-VM throughput were not measured.

Local results, source snapshots and rejected prototypes are under
`build/mem128-20261007/`. `report.html` summarizes the comparisons;
`final-v2/` contains the retained binaries, `build.json`, manifests, raw samples,
execution/compilation screens, longer repeats and original-baseline comparisons.

## Cache eviction and zero conditions (2026-10-08)

The allocator now stops eviction as soon as a guest block frees a chunk. An
eight-chunk fixture checks that extending a full cache evicts one older block
instead of seven, including multi-chunk victims, the active block at either
list end, and repeated reuse. The microbenchmark supplies its own code arena,
so it does not measure this change's effect on whole-VM recompilation.

The zero-condition comparison uses `15550bedb` as its baseline. Known lazy
results use one `CMP_Z` uop instead of four or five arithmetic uops; unknown
flags retain the helper path. Windows x64, Ryzen 9 9950X, CPU 4, GCC 15.2.0,
identical `-O2 -march=x86-64` builds and fixtures. The screen covers 32 affected
cases and ten controls, with two counterbalanced rounds of eleven 40 ms
samples and 50 ms warmup. Compilation covers one and 32 operations per block;
execution uses 32. Follow-ups use fifteen 75 ms samples and 100 ms warmup.

Representative follow-up compilation results, with 32 producer/consumer pairs
per block and known dword CMP flags:

| Consumer | Before (us/block) | After (us/block) | IR uops before / after | JIT bytes before / after |
|---|---:|---:|---:|---:|
| SETE | 6.90 | 3.87 | 320 / 192 | 1030 / 829 |
| SETNE | 6.17 | 3.87 | 288 / 192 | 980 / 829 |
| CMOVE | 7.72 | 4.44 | 320 / 192 | 1209 / 980 |
| CMOVNE | 6.89 | 4.36 | 288 / 192 | 1085 / 980 |

SETE execution improves from 0.305 to 0.237 ns/pair; SETNE and CMOVNE improve
about 13%. CMOVE improves in the screen, but its longer repeat is noisy.
No execution case is classified as a regression. There is a compilation
tradeoff: the unchanged aligned load128 control with live cycles consistently
increases from 2.95 to 3.15 us/block (6.9%), with identical IR and JIT bytes.
The corresponding store control increases about 6%, but does not pass the
consistency threshold. Some other unchanged controls improve, so their gains
should not be attributed to the new uop. These are binary- and workload-specific
microbenchmarks, not whole-VM speedups.

All ten CPU suites pass, including 1,907,952 fast-path executions. All 4,357
microbenchmark cases validate at block sizes one, 32 and 64. The full Windows
application builds. Local binaries, source diffs, raw samples and comparisons
are under `build/compile-cost-20261008/`: `zero-compare/` holds the retained
builds, `compile/` and `execute/` the screens, and `followup-compile/` and
`followup-execute/` the longer repeats.

## Narrow conditions and REP scans (2026-10-08)

Five separate changes extend the x86-64 paths above:

- Byte/word unsigned and signed conditions use native comparisons.
- Known parity uses host PF for SETcc, CMOV and JP/JNP.
- Register CMOVE/CMOVNE consumes the lazy result directly. Memory operands keep
  the previous path: fusing those too caused a repeatable RAM slowdown, despite
  producing less code. Their loads remain unconditional.
- Known ADD/INC/SUB/DEC overflow uses host arithmetic. ADC/SBB and unknown flags
  retain their helper paths. Byte operands use the low bytes of dword register
  allocations to avoid forcing both lazy operands into legacy byte registers.
- SCAS batches cached RAM comparisons within the original cycle budget, page,
  segment and address-size bounds. CMPS retains its one-element service boundary
  and uses its separate source cache without setting `is_compare` on RAM hits.

Each JIT comparison uses the commit immediately before that change, the same
fixture and GCC 15.2.0 `-O2 -march=x86-64` builds on the Ryzen 9 9950X, CPU 4.
Screens use 32 operations per block, two counterbalanced rounds, eleven 30 ms
samples and 40 ms warmup. These are selected microbenchmarks, not whole-VM gains.

| Change | Sampled execution time | Sampled compilation time |
|---|---|---|
| Narrow comparisons | Byte cases 10-21% lower; word cases roughly unchanged | 24-31% lower |
| Known parity | 48-77% lower | 15-43% lower |
| Register zero CMOV | Dword cases 30% lower; word cases roughly unchanged | 16-19% lower |
| Known overflow | 61-87% lower | Word/dword cases 20-37% lower; byte cases 10-17% higher |

The overflow byte compilation regression is a real tradeoff, despite removing
helper calls and reducing emitted code. Longer repeats use fifteen 75 ms samples
and 100 ms warmup. They confirm the execution gains and the byte CMP compilation
regression; the byte ADD compilation timing remains noisier. The unchanged RAM
load128 compilation control is about 3% higher in the repeat and does not meet
the consistency threshold. Unknown-overflow execution remains unchanged.

Local source snapshots, binaries, raw samples and rejected variants are under
`build/five-optimizations-20261008/`. The retained JIT comparisons are `1-narrow/`,
`2-parity/`, `3-cmov-register/` and `4-overflow-dword/`; the last directory also
contains the longer `followup-compile/` and `followup-execute/` comparisons.

The REP fixture adds `--bench-compare [case-index]`: 144 SCAS/CMPS combinations
cover byte/word/dword, both directions and repeat conditions, short and long RAM
ranges, unaligned accesses and MMIO. An optional zero-based case index allows
baseline/current execution to be paired per case instead of timing the entire
baseline matrix before the current one. The REP baseline is `c6d902e2a`, with
identical fixtures and `-O2` builds. Results are under `5-rep/`.

The retained REP run (`paired-v2-comparison.csv`) pairs each selected case before
moving to the next, with four rounds, alternating executable order, 100 ms per
case and CPU 4 affinity. Medians across both repeat conditions, widths and
directions reduce SCAS time by 53% for 16-element RAM scans and 76% for 4,096
items. Single-element SCAS remains unchanged. CMPS RAM medians improve 3-4%;
its main benefit is removing source-cache bookkeeping from the common path,
not batching guest instructions. MMIO medians remain within 1%. No case in the
paired screen regresses by more than 5% in every round. The `unaligned` byte
case is naturally aligned; exclude it when assessing word/dword fallbacks.

All ten CPU suites pass after each change and on the final merged tree. The
final runs include 8,781,856 fast-path executions and 15,916 REP cases. REP checks
cover stop positions, both directions, address-size wrap, non-flat segments,
independent source/destination read caches, cold lookups, MMIO, watchpoints,
traps, page/segment faults, restart PCs, lazy flags and exact cycle accounting.
All 4,357 JIT benchmark cases validate at block sizes one, 32 and 64. The full
Windows application builds. Whole-VM throughput and other host ABIs were not
measured.
