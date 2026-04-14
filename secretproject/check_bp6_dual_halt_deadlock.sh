#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
    echo "usage: $0 LOGFILE" >&2
    exit 2
fi

log_file="$1"

if [[ ! -f "$log_file" ]]; then
    echo "error: missing log file: $log_file" >&2
    exit 2
fi

awk '
function is_idle_irq_state(line) {
    return (line ~ /PIC1: irr=00 .* int_pending=0/ &&
            line ~ /PIC2: irr=00 .* int_pending=0/)
}

{
    if ($0 ~ /SMP: CPU 0 skipped \(halted, no pending int\)/)
        saw_cpu0_halt = 1

    if ($0 ~ /SMP: CPU 1 skipped \(halted, no pending int\)/)
        saw_cpu1_halt = 1

    if (!deadlock_window && saw_cpu0_halt && saw_cpu1_halt)
        deadlock_window = 1

    if (deadlock_window && $0 ~ /SMP-IRQ-STATE\[/ && is_idle_irq_state($0)) {
        idle_windows++
        if (idle_windows >= 5) {
            print "FAIL: repeated dual-halt idle windows detected" > "/dev/stderr"
            exit 1
        }
    }
}

END {
    if (deadlock_window && idle_windows > 0) {
        print "FAIL: repeated dual-halt idle windows detected" > "/dev/stderr"
        exit 1
    }

    print "PASS: no repeated dual-halt deadlock detected"
}
' "$log_file"
