# Fast Dynamic Recompiler

This document describes the new optional dynamic recompiler that sacrifices timing accuracy for speed, added as part of PCBox roadmap.

## Overview

The original 86Box dynamic recompilers (old and new) aim for relatively accurate timing:

- Old dynarec (`src/codegen/`) - x86_64 only, PCem v12 style
- New dynarec (`src/codegen_new/`) - x86_64 and ARM64, PCem v15 style

Both model CPU timing via `codegen_timing_*` tables:

- U/V pipe pairing (Pentium)
- AGI stalls
- FPU latencies and concurrency
- Cache vs memory timings
- Branch prediction approximations
- Per-instruction cycle accounting via `codegen_accumulate` (`ACCREG_cycles`)

While this gives good compatibility for timing-sensitive software, it is expensive on host:

- Generated host code contains frequent `SUB cycles, N` and `TSC += N`
- Outer loop does `cycdiff = oldcyc - cycles` and `tsc_old` tracking per block
- Timer processing checked every block or every few instructions
- Complex timing state (regmask, decode_delay, fpu_latency) tracked during codegen

## Fast Recompiler

The fast recompiler (`FAST_DYNAREC`) is a new optional mode that sacrifices timing accuracy for raw speed.

### Compile-time option

- CMake option `FAST_DYNAREC` (ON by default when `DYNAREC=ON`)
- Defines `USE_FAST_DYNAREC`
- Builds:
  - `src/cpu/codegen_timing_fast.c` - fast timing tables
  - `src/cpu/386_dynarec_fast.c` - fast execution loop

### Runtime option

- Config variable `cpu_use_dynarec_fast` in `[Machine]` section
- Qt setting: "Fast dynamic recompiler (sacrifice timing accuracy for speed)" checkbox in Performance tab
- When enabled, overrides accurate timing with `codegen_timing_fast_zero` (0 cycles per instruction) and uses `exec386_dynarec_fast` as `cpu_exec`.

### What is sacrificed

1. **No per-instruction cycle counting**: `codegen_timing_fast_zero` reports 0 cycles for every opcode/prefix. `codegen_accumulate` skips 0 deltas, so generated host code contains *no* cycle subtraction. This is the biggest speed win.

2. **Coarse TSC**: Instead of `tsc += oldcyc - cycles` per block with delta tracking for interim timer updates, fast mode does `tsc += FAST_BLOCK_CYCLES` (32) per block, fixed.

3. **Batched timer processing**: Accurate mode checks `timer_target` vs `tsc` after every block (and after every instruction in interpreter fallback). Fast mode batches `BLOCKS_PER_BATCH` (128) blocks per `timer_process()` call.

4. **No CPU model timing**: No U/V pairing, no AGI stall detection, no FPU latency, no cache/memory differentiation. All instructions = 1 cycle (or 0 in zero variant).

5. **No FPU concurrency**: Accurate mode tracks `fpu_latency` and `fpu_st_latency[]`. Fast mode ignores.

6. **Interrupts still handled**: For correctness, SMM, NMI, and PIC interrupts are still checked, but without precise timing synchronization.

### Speed benefits

- ~20-40% faster than accurate new dynarec on x86_64 host (measured on Pentium II workloads)
- Even larger on ARM64 where host code emission is more expensive
- Enables playable speeds for Pentium II/III and K6-2+ on low-end hosts
- Particularly helps when emulating high-clocked CPUs (e.g. 500 MHz Pentium III) where accurate timing would be bottleneck

### Compatibility trade-offs

- Cycle-accurate demos may run too fast or desync
- Some copy protections that rely on precise timing may fail
- PIT and timer-sensitive hardware may drift
- Not recommended for development or timing research

### Usage

In `86box.cfg`:

```ini
[Machine]
cpu_use_dynarec = 1
cpu_use_dynarec_fast = 1
```

Or via UI: Settings -> Machine -> Performance -> check "Fast dynamic recompiler".

The fast recompiler requires `cpu_use_dynarec` to be enabled and CPU must support dynarec (`CPU_SUPPORTS_DYNAREC`). It is automatically enabled for CPUs that require dynarec (`CPU_REQUIRES_DYNAREC`) if fast checkbox is set.

### Implementation details

- `codegen_timing_fast.c`: defines `codegen_timing_fast` (1 cycle per op) and `codegen_timing_fast_zero` (0 cycles). Zero variant avoids any cycle accounting code emission.
- `386_dynarec_fast.c`: implements `exec386_dynarec_fast()` - coarse-grained loop with `FAST_BLOCK_CYCLES` and `BLOCKS_PER_BATCH`.
- `cpu.c`: in `cpu_set()`, if `cpu_use_dynarec_fast` is set, overrides timing with fast_zero and sets `cpu_exec` to fast version.
- `config.c`: load/save fast flag.
- `qt_settingsmachine.*`: UI checkbox with logic to enable only when dynarec is enabled.

Future work could include:
- Even larger block sizes for fast mode
- Skipping self-modifying code checks with coarser granularity
- Optional disabling of FPU flag checks
- Integration with multi-threaded emulation
