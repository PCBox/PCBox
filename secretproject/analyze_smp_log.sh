#!/usr/bin/env bash
set -euo pipefail

LOG_MAIN="/tmp/86box_smp.log"

if [[ ! -f "$LOG_MAIN" ]]; then
    echo "error: missing log file: $LOG_MAIN" >&2
    exit 1
fi

echo "Analyzing $LOG_MAIN"
echo

awk '
function field_value(line, key,   pos, rest, end, val) {
    pos = index(line, key "=")
    if (pos == 0)
        return "?"

    rest = substr(line, pos + length(key) + 1)
    end = index(rest, " ")
    if (end == 0)
        val = rest
    else
        val = substr(rest, 1, end - 1)
    return val
}

function parse(line) {
    active = field_value(line, "active_cpu")
    num = field_value(line, "num_cpus")
    h0 = field_value(line, "cpu0.halted")
    h1 = field_value(line, "cpu1.halted")
    sipi = field_value(line, "cpu1.wait_for_sipi")
    return "active=" active " num=" num " c0.h=" h0 " c1.h=" h1 " c1.sipi=" sipi
}
{
    if (index($0, "hardresetx86:start") || index($0, "pc_reset_hard:start")) {
        hard_count++
        hard_line[hard_count] = $0
    } else if (index($0, "cpu_smp_init:end")) {
        init_count++
        init_line[init_count] = $0
    }
}
END {
    pair_count = hard_count < init_count ? hard_count : init_count
    printf("reset-start lines:        %d\n", hard_count)
    printf("cpu_smp_init:end lines:  %d\n", init_count)
    printf("paired reset cycles:     %d\n\n", pair_count)

    start = pair_count - 9
    if (start < 1) start = 1

    for (i = start; i <= pair_count; i++) {
        printf("Cycle %d\n", i)
        printf("  reset:start        -> %s\n", parse(hard_line[i]))
        printf("  cpu_smp_init:end   -> %s\n\n", parse(init_line[i]))
    }
}
' "$LOG_MAIN"
