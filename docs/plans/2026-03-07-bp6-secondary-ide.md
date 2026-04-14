# BP6 Secondary IDE Investigation Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Find and fix why the BP6 machine only exposes a usable primary IDE channel while the secondary channel is unusable in guest testing.

**Architecture:** Treat this as a root-cause debugging task, not a speculative board tweak. Reproduce the failure with a BP6 VM that intentionally places devices on the secondary channel, compare the BP6 machine wiring and PIIX4E runtime behavior against a working i440BX/PIIX4E machine, then implement the smallest code change that restores correct channel behavior without disturbing existing BP6 boot fixes.

**Tech Stack:** C, CMake, 86Box machine/chipset/IDE emulation, macOS app runtime, local shell helpers.

### Task 1: Reproduce the failure with a secondary-channel guest configuration

**Files:**
- Modify: `/tmp/BP6 IDE repro/86box.cfg` (temporary copy, not committed)
- Reference: `secretproject/capture_bp6_trial.sh`

**Step 1: Create a BP6 VM copy that puts at least one boot-irrelevant device on IDE secondary**

Use a temporary copy of the BP6 VM config so the original test VM remains untouched.

**Step 2: Run the existing emulator build against the repro VM**

Run: `bash secretproject/capture_bp6_trial.sh --vm-cfg "/tmp/BP6 IDE repro/86box.cfg" --vm-root "/tmp" --duration 8 --log /tmp/bp6_secondary_ide.log`

Expected: guest or emulator evidence clearly shows whether secondary IDE is absent, disabled, misrouted, or present-but-broken.

**Step 3: Capture the exact failure signature**

Record:
- whether the device is enumerated by BIOS/guest
- whether IDE enable messages appear for both channels
- whether base/control addresses remain legacy defaults
- whether IRQ activity appears only on primary

### Task 2: Trace BP6 machine wiring against a working reference

**Files:**
- Modify: none initially
- Reference: `src/machine/m_at_socket370.c`
- Reference: `src/chipset/intel_piix.c`
- Reference: `src/pci.c`

**Step 1: Compare BP6 slot registration with a known-good 440BX/PIIX4E board**

Read BP6 and CUBX machine init side by side and note every PCI slot-routing difference relevant to the southbridge and IDE.

**Step 2: Trace function-1 IDE enablement in `intel_piix.c`**

Confirm:
- both channels are created
- function-1 config writes enable them
- channel base/control addresses and IRQ mode are set as expected

**Step 3: Identify the first incorrect behavior**

Do not fix anything until the first divergence from the working path is concrete.

### Task 3: Add a failing regression check before the fix

**Files:**
- Create or modify: whichever minimal test/helper path best fits once the fault location is known

**Step 1: Encode the bug as a repeatable failing check**

The check can be a focused automated test if one exists nearby, or a minimal script/log assertion if emulator integration makes a unit test impractical.

**Step 2: Run the check and verify it fails for the expected reason**

Expected: failure proves the bug is real and tied to the identified root cause.

### Task 4: Implement the minimal fix

**Files:**
- Modify: exact file to be determined from investigation, likely one of:
  - `src/machine/m_at_socket370.c`
  - `src/chipset/intel_piix.c`
  - `src/disk/hdc_ide*.c`

**Step 1: Make one change that addresses the first incorrect behavior**

No cleanup or unrelated refactors.

**Step 2: Rebuild the app**

Run:
- `cmake -S . -B build -DQt5_DIR=/opt/homebrew/opt/qt@5/lib/cmake/Qt5 -DQt5LinguistTools_DIR=/opt/homebrew/opt/qt@5/lib/cmake/Qt5LinguistTools -DOpenAL_ROOT=/opt/homebrew/opt/openal-soft -DLIBSERIALPORT_ROOT=/opt/homebrew/opt/libserialport`
- `cmake --build build --parallel "$(getconf _NPROCESSORS_ONLN)"`
- `codesign -s - --entitlements src/mac/entitlements.plist --force build/src/86Box.app`

### Task 5: Verify the fix with evidence

**Files:**
- Modify: none

**Step 1: Re-run the failing secondary-channel repro**

Expected: secondary device is now usable/detected.

**Step 2: Re-run baseline BP6 boot validation**

Expected: existing BP6 boot markers still appear and no black-screen regression is introduced.

**Step 3: Confirm primary IDE still works**

Expected: original primary-channel disk/CD configuration remains functional.

**Step 4: Summarize root cause, exact file changes, and verification evidence**

Include primary OK, secondary OK, and no obvious BP6 boot regression.
