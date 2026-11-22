# Fast Dynamic Recompiler

Fast mode uses a coarse timing model for recompiled code. It skips CPU-specific timing calculations such as Pentium instruction pairing, address-generation stalls and FPU latency. Guest software that measures instruction timings can observe different results.

## Usage

Build with `DYNAREC=ON` and `FAST_DYNAREC=ON` (both default to on). For a CPU that supports recompilation, enable **Dynamic recompiler** and **Fast dynamic recompiler** in **Settings → Machine → Performance**. You can also enable both options in the machine configuration.

```ini
[Machine]
cpu_use_dynarec = 1
cpu_use_dynarec_fast = 1
```

Switching off the dynamic recompiler also disables fast timing. A build with `FAST_DYNAREC=OFF` uses the selected CPU's regular timing model when the recompiler is enabled.

## How timing works

`codegen_timing_fast` assigns a base cost of 128 cycles at the start of a compiled block. Opcodes and prefixes add no model-specific cost. The code generator carries the charge through block exits and unrolled loops. Instruction helpers and interpreted blocks retain their own cycle charges.

Both timing models use `exec386_dynarec`. It advances the timestamp counter from the cycles consumed, services device timers and delivers CPU events. A block can consume more than the remaining slice budget. The excess carries into the next slice.
