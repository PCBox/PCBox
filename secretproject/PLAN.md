# Abit BP6 Plan and Current Follow-Up Work

**Branch**: `smp-bp6`
**Last Updated**: 2026-03-07

---

## Original Goal

Implement the Abit BP6 dual-Socket 370 board in 86Box, including the SMP infrastructure that 86Box did not previously have.

That core goal is now substantially complete on this branch.

---

## Completed Milestones

### 1. Local APIC
- Local APIC MMIO implementation
- APIC timer support
- virtual wire mode support
- `IA32_APIC_BASE` wiring

### 2. I/O APIC
- real 82093AA implementation
- redirection table handling
- PIC / APIC interrupt coexistence path

### 3. Dual CPU state management
- per-CPU context storage
- save / load / switch helpers
- APIC and MSR state preservation across CPU switching

### 4. SMP execution and AP startup
- cooperative SMP execution loop
- INIT / SIPI delivery via Local APIC ICR
- AP reset-state fixes
- AP startup timing fixes
- dynarec / TLB / scheduling fixes needed to make early SMP boot stable

### 5. BP6 machine definition
- BP6 board init function
- machine table entry
- chipset / slot wiring
- BIOS bring-up on the BP6 target board

### 6. BP6 launch-to-launch boot reliability
- early divergence logging added and used to isolate the first good/bad split
- reset-path handling unified
- persisted CMOS startup state identified as the cause of alternating good / black relaunches
- BP6 CMOS normalization added in `src/nvr_at.c`

---

## Verified Current Workflow

### Build
```bash
cmake -S . -B build \
  -DQt5_DIR=/opt/homebrew/opt/qt@5/lib/cmake/Qt5 \
  -DQt5LinguistTools_DIR=/opt/homebrew/opt/qt@5/lib/cmake/Qt5LinguistTools \
  -DOpenAL_ROOT=/opt/homebrew/opt/openal-soft \
  -DLIBSERIALPORT_ROOT=/opt/homebrew/opt/libserialport
cmake --build build --parallel "$(getconf _NPROCESSORS_ONLN)"
```

### Sign for JIT-capable macOS runs
```bash
codesign -s - --entitlements src/mac/entitlements.plist --force build/src/86Box.app
```

Verified entitlements:
- `com.apple.security.cs.allow-jit`
- `com.apple.security.cs.disable-library-validation`

### Clean single-trial BP6 capture
```bash
bash secretproject/capture_bp6_trial.sh --duration 8 --log /tmp/86box_trial_1.log
```

This is the safe repro path for launch behavior because it:
- refuses to start if the same BP6 VM is already running
- captures one trial at a time
- cleans up the exact tracked emulator processes on exit

---

## Current Follow-Up Tasks

### Priority 0: resolve the current Win2000 graphical setup stall

Goal:
- determine whether the remaining BP6 blocker is fixed by the local APIC SMI-delivery change

Current known facts:
- secondary IDE on `1:0` is usable for Windows 2000 text-mode setup
- BP6 will boot that disk if BIOS HDD boot order is set to `E`
- the later stall is therefore not "secondary IDE missing"
- immediately before the stall, CPU0 sends a Local APIC ICR write `0x000C0200`
- that decodes to an SMI IPI, and the APIC SMI delivery path was previously stubbed

Current local WIP:
- `src/cpu/apic.c` now contains a minimal SMI IPI delivery implementation
- this change is not yet verified and is not committed

Resume workflow:
1. rebuild
2. codesign `build/src/86Box.app`
3. run a long traced capture of the Win2000 GUI-setup transition
4. run `bash secretproject/check_bp6_dual_halt_deadlock.sh <log>`
5. compare:
   - whether the dual-halt deadlock still occurs
   - whether the SMI IPI is now followed by real target-CPU progress
   - whether the next missing wake source is PIT, RTC, or APIC-based

Exit criteria:
- either the graphical setup hang no longer occurs
- or the first failing post-SMI behavior is proven with trace evidence
### Priority 1: manual reset validation

Goal:
- confirm GUI hard reset behavior stays reliable after the reset-path cleanup

Recommended flow:
1. clean build + sign
2. run `secretproject/run_bp6_smp_reset_test.sh --clean-logs`
3. in the app, trigger two consecutive hard resets
4. run `secretproject/analyze_smp_log.sh`
5. inspect `/tmp/86box_smp.log` and `/tmp/86box_smp_boot.log`

### Priority 2: branch cleanup policy

Goal:
- decide what to keep before merge

Questions to resolve:
- which diagnostics are permanent and useful
- which traces should be removed once validation is complete
- whether the helper scripts remain in-tree or move elsewhere

### Priority 3: broader BP6 validation

Goal:
- move from BIOS bring-up confidence to broader machine confidence

Areas:
- OS-level SMP boot validation
- PCI routing / slot behavior checks
- optional HPT366 follow-up work if desired later

---

## Things No Longer Considered Blockers

These were former blockers on the branch and are now resolved:
- APIC MMIO mapping surviving `mem_reset()`
- BSP-to-AP SIPI timing handoff during startup
- AP reset state being derived incorrectly from BSP state
- AP MSR / APIC base state drift
- launch-to-launch BP6 black-screen alternation caused by persisted CMOS bytes
- split hard-reset handling causing unreliable reset behavior

---

## Reference Commits

- `307130f4e` — Add BP6 SMP debug artifacts and warning fix
- `d36c302fc` — Fix BP6 SMP boot and reset handling
- `e03629a00` — Fix AP eflags VM_FLAG bug, sync `msr.apic_base`, clean up diagnostics
- `19d62bf43` — Fix AP MSR state: copy BSP MSRs to AP, preserve across INIT
- `cca786670` — Fix context switch leak: save/restore `use32`, `stack32`, `nmi`, `trap`
- `6648d0d56` — Fix AP reset state: use proper x86 hardware reset instead of BSP copy
- `cc1198299` — Fix SIPI timing: fine-grained time slicing for AP startup handshake
- `6d6e9e478` — Fix SIPI timing: force BSP to yield time slice after SIPI delivery
- `c534a3593` — Fix APIC MMIO mapping destroyed by `mem_reset()`
- `a4977bb63` — Phase 5: BP6 machine definition
- `b43521a45` — Phase 3: dual CPU state management
- `2cc2c31ea` — Phase 2: implement I/O APIC
- `13bc0c275` — Phase 1: implement Local APIC
