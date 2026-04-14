#!/usr/bin/env bash
set -euo pipefail

APP_BUNDLE="/Users/anthony/projects/code/86Box-voodoo-arm64/build/src/86Box.app"
VM_ROOT="/Users/anthony/Library/Application Support/86Box/Virtual Machines"
VM_CFG="/Users/anthony/Library/Application Support/86Box/Virtual Machines/BP6 SMP Test/86box.cfg"
LOG_MAIN="/tmp/86box_smp.log"
LOG_BOOT="/tmp/86box_smp_boot.log"

clean_logs=0
if [[ "${1:-}" == "--clean-logs" ]]; then
    clean_logs=1
fi

if [[ ! -d "$APP_BUNDLE" ]]; then
    echo "error: missing app bundle: $APP_BUNDLE" >&2
    exit 1
fi

if [[ ! -f "$VM_CFG" ]]; then
    echo "error: missing VM config: $VM_CFG" >&2
    exit 1
fi

if [[ "$clean_logs" -eq 1 ]]; then
    rm -f "$LOG_MAIN" "$LOG_BOOT"
    echo "Cleared $LOG_MAIN and $LOG_BOOT"
fi

echo "Launching 86Box BP6 VM..."
open -n "$APP_BUNDLE" --args -P "$VM_ROOT" "$VM_CFG"

cat <<'EOF'
Launch command sent.

Next:
1. In 86Box, trigger two consecutive hard resets.
2. Run:
   secretproject/analyze_smp_log.sh
EOF
