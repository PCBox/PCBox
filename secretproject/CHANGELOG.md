# SMP / Abit BP6 — Changelog

Entries are in reverse chronological order.

---

## 2026-03-07

### Uncommitted BP6 Win2000 / secondary IDE investigation notes

Working-tree findings recorded before commit:
- BP6 secondary IDE is present and usable; it is not missing.
- Windows 2000 text-mode setup can detect and copy files to a HDD on IDE secondary (`1:0`).
- BP6 BIOS must be configured to boot HDD `E` in order to boot that secondary disk.
- The remaining failure is later: Win2000 stalls during graphical setup after the splash / blue overlay stage.
- Long trace evidence shows the final notable transition is a Local APIC ICR write `0x000C0200`, decoded as an SMI IPI.
- At the time of the failing trace, the Local APIC SMI IPI path in `src/cpu/apic.c` was stubbed.
- A minimal local fix has been added in the working tree so SMI IPIs update the target CPU's saved SMI line and wake a halted target CPU.
- That local APIC SMI change is not yet verified and has not been committed.

### Warning fix and debug artifact update

**Commit**: `307130f4e` — Add BP6 SMP debug artifacts and warning fix

Changes:
- Added `secretproject/` branch notes and helper scripts to the repo.
- Narrowed `.gitignore` from ignoring all of `secretproject/` to ignoring only `secretproject/*.bin`.
- Fixed `src/nvr_at.c` IRQ sentinel decoding by resolving raw `0xff` / `0xfe` values before assigning to `int8_t nvr->irq`.

Result:
- The two `nvr_at.c` tautological compare warnings are gone.
- `NVR_IRQ_CONFIG` decoding is now structurally correct.

---

## 2026-03-06

### BP6 black-screen boot and reset-path fix

**Commit**: `d36c302fc` — Fix BP6 SMP boot and reset handling

Key fixes:
- Unified hard reset processing through `pc_process_hard_reset_pending()` so reset work is no longer split across independent paths.
- Identified the first meaningful good/bad divergence before `F000:7DA9`.
- Determined the alternating good/black launch pattern was caused by persisted BP6 CMOS state, not APIC timing.
- Normalized BP6 startup CMOS in `src/nvr_at.c`:
  - `CMOS[0x38] |= 0x01`
  - `CMOS[0x39] |= 0x08`
- Added BP6-compatible `0x39` handling in the existing NVR special-case logic.

Verification recorded during development:
- six clean helper-launched BP6 trials
- all six classified GOOD
- no `F000:7DA9` error-halt hit in that sample

### AP state / diagnostics cleanup

**Commit**: `e03629a00` — Fix AP eflags VM_FLAG bug, sync `msr.apic_base`, clean up diagnostics

Changes:
- fixed AP `EFLAGS.VM` state handling
- kept APIC base MSR synchronized correctly across CPU state
- cleaned up and narrowed active diagnostics

### AP startup / context fixes

**Commits**:
- `8527d5a64` — Fix AP ring buffer trace: increase to 4096 entries, add watchdog early-dump trigger
- `19d62bf43` — Fix AP MSR state: copy BSP MSRs to AP, preserve across INIT
- `094414b0d` — Add AP instruction-level ring buffer trace for SMP debugging
- `cca786670` — Fix context switch leak: save/restore `use32`, `stack32`, `nmi`, `trap`
- `6648d0d56` — Fix AP (CPU1) reset state: use proper x86 hardware reset instead of BSP copy
- `cc1198299` — Fix SIPI timing: fine-grained time slicing for AP startup handshake
- `0c631a35f` — Fix VGA hang in SMP execution loop: dynarec, TLB, stuck detection
- `6d6e9e478` — Fix SIPI timing: force BSP to yield time slice after SIPI delivery

These changes collectively moved the branch from early AP startup failures to stable BP6 SMP BIOS progress.

---

## 2026-03-05

### APIC MMIO repair

**Commit**: `c534a3593` — Fix APIC MMIO mapping destroyed by `mem_reset()`

Changes:
- re-added APIC mapping after `mem_reset()`
- marked the MMIO region with internal memory state so the handlers were reachable again

### BP6 machine definition

**Commit**: `a4977bb63` — Phase 5: BP6 machine definition

Added:
- `machine_at_bp6_init()`
- BP6 machine table entry
- PCI / AGP / ISA board wiring
- BP6 BIOS ROM usage on the branch

### Dual CPU state management

**Commit**: `b43521a45` — Phase 3: dual CPU state management

Added:
- per-CPU context storage
- save / load / switch helpers
- core SMP globals and lifecycle

### I/O APIC implementation

**Commits**:
- `2cc2c31ea` — Phase 2: implement I/O APIC (Intel 82093AA)
- `33aa4fee7` — Fix `fpu_state_t` build error: include `x87_sf.h` in `cpu.h`

Added:
- real I/O APIC register implementation
- redirection table logic
- interrupt routing integration with existing PIC paths

### Local APIC implementation

**Commit**: `13bc0c275` — Phase 1: implement Local APIC

Added:
- `src/cpu/apic.c`
- `src/include/86box/apic.h`
- APIC MMIO register set
- virtual wire support
- APIC timer support
- `IA32_APIC_BASE` integration

### Branch start

**Commit**: `a080d1fb1`

- Branch `smp-bp6` created from the upstream code line.
