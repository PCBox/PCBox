# Abit BP6 Motherboard — Feasibility Study and Outcome Update

## Outcome Update (2026-03-07)

The original feasibility conclusion was correct: the board itself was easy, and the real work was SMP infrastructure.

That infrastructure now exists on branch `smp-bp6`.

Implemented on this branch:
- Local APIC support
- I/O APIC support
- dual-CPU state management
- cooperative SMP execution and AP startup via INIT/SIPI
- BP6 machine definition and BIOS bring-up
- reset-path cleanup for hard reset handling
- BP6-specific startup CMOS normalization fixing the alternating good/black launch failure

Current verdict:
- **Feasibility proven** for BP6 bring-up in 86Box on this branch.
- The work is no longer blocked by missing core SMP architecture.
- Remaining effort is validation, cleanup, and optional board enhancements.

What is still not fully claimed here:
- broad OS-level SMP validation matrix
- exhaustive GUI hard-reset stress validation
- optional HPT366 support

---

## Original Executive Summary

The Abit BP6 is a dual Socket 370 motherboard (1999) using the Intel 440BX chipset, famous for enabling cheap SMP with paired Celeron Mendocino CPUs. Implementing it in 86Box is **feasible but requires significant core infrastructure work** — specifically, 86Box currently has **zero multi-CPU support**. The board-level components (440BX, PIIX4E, W83977EF, W83782D) are all already implemented. The blocking work is SMP: Local APIC, I/O APIC (real implementation, not the current stub), dual CPU state management, and a multi-CPU execution model.

Original verdict:
- the machine definition itself is trivial
- SMP infrastructure is the real project
- HPT366 can be deferred

That high-level conclusion matched the actual implementation path taken on this branch.

---

## What the Branch Proved

### 1. Board-level reuse was sufficient

The existing 440BX / PIIX4E / Super I/O / HW monitor support was enough to define and boot the BP6 board once SMP support existed.

### 2. The real blockers were all in the SMP core

The branch had to add or repair:
- Local APIC behavior
- I/O APIC routing
- per-CPU context storage and switching
- AP reset semantics
- AP startup timing after SIPI
- AP MSR / APIC base handling
- APIC MMIO lifetime across machine reset paths

### 3. BIOS-facing edge cases mattered as much as the core architecture

The final intermittent black-screen issue was not caused by the original expected APIC architecture work. It was caused by BP6-specific persisted CMOS startup state:
- black relaunches diverged early and halted at `F000:7DA9`
- good relaunches reached APIC setup and continued to `linear=000C462D`
- startup normalization of CMOS bytes `0x38` and `0x39` resolved the launch alternation observed during debugging

### 4. Repro tooling mattered

The branch needed dedicated helper scripts to avoid contaminating repro attempts with stacked emulator processes. The clean single-trial capture helper is now part of the working workflow.

---

## Remaining Gaps

These are no longer feasibility blockers, only follow-up work:
- broader OS validation on BP6 SMP guests
- manual reset stress coverage in the GUI
- deciding which diagnostics stay in-tree
- optional HPT366 implementation

---

## Original Study Notes

The rest of this file preserves the original feasibility study context that guided the implementation.

### Board hardware summary

| Component | Part | Status in 86Box |
|-----------|------|-----------------|
| Northbridge | Intel 82443BX (440BX) | Implemented (`src/chipset/intel_4x0.c`) |
| Southbridge | Intel 82371EB (PIIX4E) | Implemented (`src/chipset/intel_piix.c`) |
| I/O APIC | Intel 82093AA | Was stub-only at study time |
| Super I/O | Winbond W83977EF | Implemented (`src/sio/sio_w83977.c`) |
| HW Monitor | Winbond W83782D | Implemented (`src/device/hwm_lm78.c`) |
| IDE (extra) | HighPoint HPT366 (UDMA/66) | Still optional / not required for basic bring-up |
| CPU Socket | Dual Socket 370 (PPGA) | Implemented on this branch |
| Memory | 3x DIMM, PC100 SDRAM, 768MB max | Implemented |
| BIOS | Award PnP BIOS | Available and used on branch |
| AGP | 1x AGP 2x (3.3V) | Implemented |
| PCI | 5x PCI 2.1 | Implemented |
| ISA | 2x ISA 16-bit | Implemented |

### Main lesson from the study

The BP6 was never primarily a board-definition problem. It was an emulator-core SMP problem first, and that framing turned out to be exactly right.
