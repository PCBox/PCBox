#!/usr/bin/env bash
set -euo pipefail

APP_BIN="${APP_BIN:-/Users/anthony/projects/code/86Box-voodoo-arm64/build/src/86Box.app/Contents/MacOS/86Box}"
VM_ROOT="${VM_ROOT:-/Users/anthony/Library/Application Support/86Box/Virtual Machines}"
VM_CFG="${VM_CFG:-/Users/anthony/Library/Application Support/86Box/Virtual Machines/BP6 SMP Test/86box.cfg}"
TRIAL_DURATION="${TRIAL_DURATION:-8}"
LOG_FILE="${LOG_FILE:-/tmp/86box_direct.log}"
STARTUP_SETTLE="${STARTUP_SETTLE:-1}"

usage() {
    cat <<'EOF'
Usage: capture_bp6_trial.sh [options]

Options:
  --app-bin PATH     86Box executable to launch
  --vm-root PATH     VM root passed with -P
  --vm-cfg PATH      VM config path
  --duration SEC     Seconds to let the trial run
  --log PATH         Stderr/stdout capture path
  --help             Show this help
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --app-bin)
            APP_BIN="$2"
            shift 2
            ;;
        --vm-root)
            VM_ROOT="$2"
            shift 2
            ;;
        --vm-cfg)
            VM_CFG="$2"
            shift 2
            ;;
        --duration)
            TRIAL_DURATION="$2"
            shift 2
            ;;
        --log)
            LOG_FILE="$2"
            shift 2
            ;;
        --help)
            usage
            exit 0
            ;;
        *)
            echo "unknown argument: $1" >&2
            usage >&2
            exit 1
            ;;
    esac
done

if [[ ! -x "$APP_BIN" ]]; then
    echo "missing executable app binary: $APP_BIN" >&2
    exit 1
fi

if [[ ! -f "$VM_CFG" ]]; then
    echo "missing VM config: $VM_CFG" >&2
    exit 1
fi

vm_match="$(basename "$(dirname "$VM_CFG")")"
launcher_pid=""
captured_pids=()

collect_matching_pids() {
    /bin/ps -ww -axo pid=,ppid=,command= | awk -v app="$APP_BIN" -v vm="$vm_match" -v self="$$" -v parent="$PPID" '
        index($0, app) &&
        index($0, vm) &&
        index($0, "capture_bp6_trial.sh") == 0 &&
        index($0, "test_capture_bp6_trial.sh") == 0 &&
        $3 != "awk" &&
        $1 != self &&
        $1 != parent { print $1 }
    '
}

append_unique_pid() {
    local pid="$1"
    local existing

    [[ -n "$pid" ]] || return 0

    for existing in "${captured_pids[@]:-}"; do
        if [[ "$existing" == "$pid" ]]; then
            return 0
        fi
    done

    captured_pids+=("$pid")
}

capture_new_pids() {
    local pid
    local before_blob after_blob

    before_blob="${1:-}"
    after_blob="${2:-}"

    while read -r pid; do
        [[ -n "$pid" ]] || continue
        if ! grep -qx "$pid" <<<"$before_blob"; then
            append_unique_pid "$pid"
        fi
    done <<<"$after_blob"
}

pid_is_live() {
    local pid="$1"
    kill -0 "$pid" 2>/dev/null
}

kill_pid_list() {
    local signal="$1"
    local pid

    for pid in "${captured_pids[@]:-}"; do
        pid_is_live "$pid" || continue
        /bin/kill "-$signal" "$pid" 2>/dev/null || true
    done
}

cleanup() {
    if [[ -n "$launcher_pid" ]]; then
        append_unique_pid "$launcher_pid"
    fi

    if [[ "${#captured_pids[@]}" -eq 0 ]]; then
        return 0
    fi

    kill_pid_list TERM
    sleep 1
    kill_pid_list KILL
}

existing_pids="$(collect_matching_pids)"
if [[ -n "$existing_pids" ]]; then
    echo "matching 86Box instance already running for '$vm_match'; stop it before starting a trial" >&2
    echo "$existing_pids" >&2
    /bin/ps -ww -axo pid=,ppid=,command= | awk -v app="$APP_BIN" -v vm="$vm_match" -v self="$$" -v parent="$PPID" '
        index($0, app) &&
        index($0, vm) &&
        index($0, "capture_bp6_trial.sh") == 0 &&
        index($0, "test_capture_bp6_trial.sh") == 0 &&
        $3 != "awk" &&
        $1 != self &&
        $1 != parent { print }
    ' >&2
    exit 1
fi

trap cleanup EXIT INT TERM

mkdir -p "$(dirname "$LOG_FILE")"
: >"$LOG_FILE"

"$APP_BIN" -P "$VM_ROOT" "$VM_CFG" >"$LOG_FILE" 2>&1 &
launcher_pid="$!"

sleep "$STARTUP_SETTLE"
capture_new_pids "$existing_pids" "$(collect_matching_pids)"

if [[ "${#captured_pids[@]}" -eq 0 ]]; then
    append_unique_pid "$launcher_pid"
fi

echo "capture trial started: vm='$vm_match' log='$LOG_FILE' pids=${captured_pids[*]}" >&2

sleep "$TRIAL_DURATION"

echo "capture trial finished: vm='$vm_match' log='$LOG_FILE'" >&2
