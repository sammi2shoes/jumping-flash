#!/usr/bin/env bash
set -euo pipefail
project_dir="$(cd "$(dirname "$0")/.." && pwd)"
cd "$project_dir"
check_dir="$(mktemp -d "${TMPDIR:-/tmp}/jumping-flash-check.XXXXXX")"
trap 'rm -rf "$check_dir"' EXIT
for suite in dual-position-host direction-host ax12-ble-host telemetry-host jog-host legacy-setup-host legacy-watchdog-host post-update-host motor-recovery-host; do
  g++ -std=c++17 -I"tests/$suite" "tests/$suite/test.cpp" -o "$check_dir/$suite"
  "$check_dir/$suite"
done
g++ -std=c++17 -Itests/jog-host tests/position-step-host/test.cpp -o "$check_dir/position-step"
"$check_dir/position-step"
g++ -std=c++17 -Itests/post-update-host tests/watchdog-observer-host/test.cpp -o "$check_dir/watchdog-observer"
"$check_dir/watchdog-observer"
cc -Wall -Wextra -Werror tests/native-uart/port_test.c -o "$check_dir/native-uart"
"$check_dir/native-uart"
python3 tests/test_watchdog_disconnect.py
python3 -m py_compile scripts/setup.py scripts/log_serial.py scripts/watchdog_disconnect.py reference/rs05_control_gui.py
