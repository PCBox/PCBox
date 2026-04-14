# SMP / Abit BP6 — Current Status

**Branch**: `smp-bp6`
**Started**: 2026-03-05
**Last Updated**: 2026-03-07

---

## Executive Summary

The branch is no longer blocked on the original BP6 SMP black-screen boot failure.

Current state:
- Local APIC, I/O APIC, dual-CPU context switching, cooperative SMP execution, and BP6 machine definition are implemented.
- The intermittent BP6 "good / black / good / black" relaunch behavior was traced to persisted CMOS state, not APIC timing.
- Hard reset handling was unified so the main loop and Qt idle path use the same hard-reset processing path.
- Clean launch testing no longer reproduces the black-screen boot on this branch.

The remaining work is no longer just validation and cleanup.

Current active investigation:
- BP6 secondary IDE is present and usable.
- A disk on IDE secondary (`1:0`) is visible to BIOS and to Windows 2000 text-mode setup.
- BP6 can boot that secondary disk if BIOS HDD boot order is explicitly set to `E`.
- The remaining blocker is a later Windows 2000 graphical setup hang, not a missing secondary IDE channel.

Current highest-confidence root-cause candidate:
- Win2000 sends a Local APIC SMI IPI immediately before the graphical setup stall.
- The Local APIC SMI delivery path in `src/cpu/apic.c` was still stubbed during the captured failing trace.
- A minimal SMI-delivery implementation has now been added locally, but it has not yet been verified through the full multi-minute hang window.

---

## Phase Summary

```
Phase 1: Local APIC            [##########] 100%  DONE
Phase 2: I/O APIC (82093AA)    [##########] 100%  DONE
Phase 3: Dual CPU State        [##########] 100%  DONE
Phase 4: SMP Execution & IPI   [##########] 100%  DONE
Phase 5: BP6 Machine Def       [##########] 100%  DONE
Phase 6: Validation / Cleanup  [######....]  60%  IN PROGRESS
```

---

## Current Investigation Snapshot

### Secondary IDE characterization

What is now established:
- BP6 is using the PIIX4E IDE controller with both channels present.
- The secondary channel is not missing.
- The secondary channel is not merely disabled.
- Secondary IDE I/O is live enough for Windows 2000 text-mode setup to detect the disk and copy install files.

What failed originally:
- Booting the installed disk from `1:0` failed until BP6 BIOS HDD boot order was changed to `E`.

What that means:
- The earlier "secondary IDE broken" report was partly a BP6 BIOS boot-order issue.
- Secondary IDE is usable as a device channel.
- The remaining failure is later in the Win2000 graphical boot/setup path.

### Win2000 graphical hang

Reproduced behavior:
- After text-mode setup and reboot, Win2000 reaches the splash / blue overlay phase and stalls.
- User observation at the stall: no visible disk activity.

Captured trace evidence:
- Secondary IDE reads continue deep into graphical setup before the stall.
- RTC periodic IRQ8 activity is still present immediately before the transition.
- PIC masks then change to `PIC1=FA`, `PIC2=FE`.
- After that point, both CPUs repeatedly remain halted with no pending interrupt in the scheduler trace.
- Immediately before the stall, CPU0 issues:
  - `APIC write [300] = 000C0200`
  - decoded as Local APIC ICR delivery mode `2` (SMI), shorthand `3` (all excluding self)
- At that moment, the APIC SMI IPI path was a stub.

Current interpretation:
- The first clearly incorrect runtime behavior is in Local APIC SMI delivery, not in secondary IDE channel creation.
- The latest local change implements APIC SMI delivery to the target CPU context, but that change is still unverified.

---

## What Changed Most Recently

### 1. Reset handling unified

Problem:
- Hard reset could take two clicks because reset work was split between different paths.

Fix:
- Added shared hard-reset processing in `pc_process_hard_reset_pending()`.
- `pc_run()` and the Qt idle path now use the same reset processing logic.

Result:
- The reset path is structurally consistent again.
- Manual GUI reset stress testing is still the main remaining validation item.

### 2. BP6 black boot root cause identified

Observed divergence:
- Good runs reached APIC setup and continued to `linear=000C462D`.
- Black runs diverged earlier and halted in the BIOS error loop at `F000:7DA9`.

Root cause:
- The first meaningful divergence was persisted CMOS state.
- BP6 startup behavior depended on CMOS bytes `0x38` and `0x39`.
- Bad relaunches started from `0x38=0x80` and/or `0x39=0x00`.
- Good relaunches started from `0x38=0x81/0x83` and `0x39=0x08`.

Fix:
- BP6 startup normalization in `src/nvr_at.c`:
  - `CMOS[0x38] |= 0x01`
  - `CMOS[0x39] |= 0x08`
- BP6 now also forces the expected `0x39` bit on read/write, matching the existing BX6-style handling.

### 3. Warning fix in `nvr_at.c`

Problem:
- `nvr->irq` is `int8_t`, but the code compared it to `0xff` and `0xfe`, producing tautological warnings and leaving the `NVR_IRQ_CONFIG` branch effectively dead.

Fix:
- Decode the raw IRQ sentinel byte into a `uint8_t` first.
- Resolve `0xff` / `0xfe` before narrowing to `int8_t`.

Result:
- The warning is gone.
- The sentinel decoding now matches the actual encoding in `info->local`.

---

## Verification Evidence

### Boot behavior

Clean helper-launched trials after the CMOS fix:
- 6 consecutive GOOD runs
- 0 observed `F000:7DA9` black boots in that sample

Good-run markers:
- APIC writes at `FEE000F0`, `FEE00350`, `FEE00360`
- BSP reaches `linear=000C462D`

Black-run signature that is no longer reproduced in those clean trials:
- `CPU0 HIT ERROR HALT (EB FE)` at `linear 000F7DA9`
- AP stays `wait_for_sipi=1`
- BIOS APIC setup writes absent

### Build / sign

Fresh local verification completed on this branch:
- clean CMake configure
- full rebuild with all host cores
- ad-hoc codesign of `build/src/86Box.app`
- verified entitlements:
  - `com.apple.security.cs.allow-jit`
  - `com.apple.security.cs.disable-library-validation`

---

## Useful Scripts

### `secretproject/capture_bp6_trial.sh`

Purpose:
- Launch exactly one BP6 trial.
- Capture stdout/stderr to a chosen log.
- Track and clean up the exact spawned emulator processes.

Use this instead of ad hoc `nohup`/`pkill` loops.

Example:
```bash
bash secretproject/capture_bp6_trial.sh --duration 8 --log /tmp/86box_trial_1.log
```

### `secretproject/test_capture_bp6_trial.sh`

Purpose:
- Validates that the capture helper cleans up its launched processes.

### `secretproject/run_bp6_smp_reset_test.sh`

Purpose:
- Launch the BP6 app for manual reset testing.
- Intended workflow:
  1. launch app
  2. trigger two consecutive hard resets in the UI
  3. analyze `/tmp/86box_smp.log`

### `secretproject/analyze_smp_log.sh`

Purpose:
- Summarize recent reset / `cpu_smp_init:end` cycles from `/tmp/86box_smp.log`.

---

## Remaining Work

### High priority

1. Verify the uncommitted APIC SMI delivery change
- Re-run the long Win2000 graphical-setup capture.
- Confirm whether the post-splash dual-halt deadlock disappears.
- If the hang changes but remains, trace the next missing wake source after SMI delivery.

2. Manual reset validation
- Run the BP6 VM from the signed app.
- Exercise repeated hard resets from the UI.
- Confirm there is no regression in reset responsiveness.

### Medium priority

3. Diagnostics cleanup policy
- Keep the current diagnostic code for now.
- Decide later which traces are permanent and which should be removed before merge.

4. BP6 board validation beyond boot
- PCI slot / routing validation against BIOS expectations.
- OS-level SMP confirmation beyond BIOS/APIC bring-up.

---

## Key Commits

| Commit | Summary |
|--------|---------|
| `307130f4e` | Add BP6 SMP debug artifacts and warning fix |
| `d36c302fc` | Fix BP6 SMP boot and reset handling |
| `e03629a00` | Fix AP eflags VM_FLAG bug, sync `msr.apic_base`, clean up diagnostics |
| `19d62bf43` | Fix AP MSR state: copy BSP MSRs to AP, preserve across INIT |
| `cca786670` | Fix context switch leak: save/restore `use32`, `stack32`, `nmi`, `trap` |
| `6648d0d56` | Fix AP reset state: use proper x86 hardware reset instead of BSP copy |
| `cc1198299` | Fix SIPI timing: fine-grained time slicing for AP startup handshake |
| `6d6e9e478` | Fix SIPI timing: force BSP to yield time slice after SIPI delivery |
| `c534a3593` | Fix APIC MMIO mapping destroyed by `mem_reset()` |
| `a4977bb63` | Phase 5: BP6 machine definition |
| `b43521a45` | Phase 3: dual CPU state management |
| `2cc2c31ea` | Phase 2: implement I/O APIC |
| `13bc0c275` | Phase 1: implement Local APIC |
