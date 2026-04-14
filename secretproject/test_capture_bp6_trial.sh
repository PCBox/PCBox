#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CAPTURE_SCRIPT="$ROOT_DIR/secretproject/capture_bp6_trial.sh"

tmpdir="$(mktemp -d)"
cleanup() {
    if [[ -n "${fake_app:-}" && -f "$fake_app" ]]; then
        /bin/ps -ww -axo pid=,command= | awk -v app="$fake_app" 'index($0, app) { print $1 }' | while read -r pid; do
            [[ -n "$pid" ]] && /bin/kill -9 "$pid" 2>/dev/null || true
        done
    fi
    rm -rf "$tmpdir"
}
trap cleanup EXIT

fake_vm_dir="$tmpdir/fake-vm"
mkdir -p "$fake_vm_dir"
touch "$fake_vm_dir/86box.cfg"

fake_app="$tmpdir/fake-86box.sh"
cat >"$fake_app" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail

if [[ "${1:-}" == "--worker" ]]; then
    trap 'exit 0' TERM INT
    while true; do
        sleep 1
    done
fi

vm_name="${*: -1}"
echo "fake emulator started for $vm_name"
"$0" --worker "$vm_name" >/dev/null 2>&1 &
"$0" --worker "$vm_name" >/dev/null 2>&1 &

trap 'exit 0' TERM INT
while true; do
    sleep 1
done
EOF
chmod +x "$fake_app"

log_file="$tmpdir/trial.log"

"$CAPTURE_SCRIPT" \
    --app-bin "$fake_app" \
    --vm-root "$tmpdir" \
    --vm-cfg "$fake_vm_dir/86box.cfg" \
    --duration 1 \
    --log "$log_file"

if [[ ! -s "$log_file" ]]; then
    echo "expected non-empty log file: $log_file" >&2
    exit 1
fi

if /bin/ps -ww -axo pid=,command= | awk -v app="$fake_app" -v vm="fake-vm" 'index($0, app) && index($0, vm) { found=1 } END { exit found ? 0 : 1 }'
then
    echo "capture script left fake emulator processes running" >&2
    exit 1
fi

echo "capture harness cleanup test passed"
