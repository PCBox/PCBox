#!/bin/bash
# bench.sh [SECS]: Start > Shut Down in the guest (if it runs), install build-static's exe in
# $NV3_WORK/rig, boot it (minimized; NV3_GPU / NV3_3D_VERIFY pass through), start 3DMark 99 MAX's
# benchmark and log "dev nv3 3d" 30 s into Game 1 and SECS (40) s later, with a shot each time.
# The guest must be at the desktop with nothing open.
S=$(dirname "$0"); T=$S/nvtest.sh
SECS=${1:-40}
running() { powershell -NoProfile -ExecutionPolicy Bypass -File "$S/test-procs.ps1" | tr -dc 0-9; }
if [ "$(running)" != "0" ]; then
    # the p55tvp4 has no soft-off: Windows stops at "safe to turn off", then the emulator is stopped
    $T cmd "key 1d,01,81,9d"; sleep 1; $T cmd "type u"; sleep 2; $T cmd "type s"; sleep 1; $T cmd "key 1c,9c"
    sleep 25; $T stop >/dev/null
fi
for i in $(seq 1 20); do cp "$S/../../build-static/src/86Box-Next.exe" "$NV3_WORK/rig/" 2>/dev/null && break; sleep 1; done
cmp -s "$S/../../build-static/src/86Box-Next.exe" "$NV3_WORK/rig/86Box-Next.exe" || { echo "could not install the exe" >&2; exit 1; }
$T start >/dev/null; sleep 75
$T cmd "key 1c,9c"; sleep 3                     # network login
"$S/3dmark.sh"; sleep 15
$T cmd "key 1c,9c"; sleep 6                     # New 3DMark Benchmark
$T cmd "key 1c,9c"; sleep 30                    # Benchmark
$T cmd "dev nv3 3d"; $T shot bench1 >/dev/null; sleep "$SECS"
$T cmd "dev nv3 3d"; $T shot bench2 >/dev/null; sleep 1
grep -a "nv3 3D\|NV3 GPU" "$NV3_WORK/rig/nv3.log" | tail -n 20
