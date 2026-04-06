#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
SESSION_NAME="rps-cli-vg-$$"
LOG_DIR="${REPO_ROOT}/build/tests/cli_valgrind"

cleanup() {
  tmux kill-session -t "${SESSION_NAME}" 2>/dev/null || true
  pkill -f "/build/bin/server" 2>/dev/null || true
  pkill -f "/build/bin/client_text" 2>/dev/null || true
}
trap cleanup EXIT

if ! command -v valgrind >/dev/null 2>&1; then
  echo "SKIP: valgrind not found; skipping cli valgrind test."
  exit 0
fi

if ! command -v tmux >/dev/null 2>&1; then
  echo "SKIP: tmux not found; skipping cli valgrind test."
  exit 0
fi

cd "${REPO_ROOT}"
pkill -f "/build/bin/server" 2>/dev/null || true
pkill -f "/build/bin/client_text" 2>/dev/null || true
rm -rf "${LOG_DIR}"
mkdir -p "${LOG_DIR}"

vg_cmd='valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes --num-callers=30'

echo "Starting CLI valgrind test in tmux session ${SESSION_NAME}"

tmux new-session -d -s "${SESSION_NAME}" -n backend

tmux set-option -t "${SESSION_NAME}" automatic-rename off
tmux set-option -t "${SESSION_NAME}" allow-rename off

tmux send-keys -t "${SESSION_NAME}:backend.0" "${vg_cmd} --log-file=${LOG_DIR}/server.log ./build/bin/server" C-m
sleep 1

if ! pgrep -f "./build/bin/server" >/dev/null 2>&1; then
  echo "FAIL: server valgrind process did not stay running."
  tmux capture-pane -p -t "${SESSION_NAME}:backend.0" | tail -n 30 || true
  exit 1
fi

tmux new-window -d -t "${SESSION_NAME}:1" -n players
tmux split-window -h -t "${SESSION_NAME}:1.0"
tmux split-window -v -t "${SESSION_NAME}:1.0"
tmux split-window -v -t "${SESSION_NAME}:1.1"
tmux select-layout -t "${SESSION_NAME}:1" tiled

tmux send-keys -t "${SESSION_NAME}:1.0" "${vg_cmd} --log-file=${LOG_DIR}/client1.log ./build/bin/client_text alpha R 1 1" C-m
tmux send-keys -t "${SESSION_NAME}:1.1" "${vg_cmd} --log-file=${LOG_DIR}/client2.log ./build/bin/client_text beta P 2 2" C-m
tmux send-keys -t "${SESSION_NAME}:1.2" "${vg_cmd} --log-file=${LOG_DIR}/client3.log ./build/bin/client_text gamma S 3 3" C-m
tmux send-keys -t "${SESSION_NAME}:1.3" "${vg_cmd} --log-file=${LOG_DIR}/client4.log ./build/bin/client_text delta R 4 4" C-m

sleep 10

for pattern in \
  "./build/bin/client_text alpha R 1 1" \
  "./build/bin/client_text beta P 2 2" \
  "./build/bin/client_text gamma S 3 3" \
  "./build/bin/client_text delta R 4 4"
do
  if ! pgrep -f "${pattern}" >/dev/null 2>&1; then
    echo "FAIL: expected running valgrind client missing: ${pattern}"
    exit 1
  fi
done

for pane in 0 1 2 3; do
  tmux send-keys -t "${SESSION_NAME}:1.${pane}" q
done

for _ in $(seq 1 80); do
  if ! pgrep -f "/build/bin/client_text" >/dev/null 2>&1; then
    break
  fi
  sleep 0.2
done

tmux send-keys -t "${SESSION_NAME}:backend.0" C-c
sleep 2

required_logs=(server.log client1.log client2.log client3.log client4.log)
failed=0

for log_name in "${required_logs[@]}"; do
  log_file="${LOG_DIR}/${log_name}"
  if [[ ! -s "${log_file}" ]]; then
    echo "FAIL: missing valgrind log ${log_file}"
    failed=1
    continue
  fi

  if grep -Eq "Invalid read|Invalid write|Use of uninitialised|Conditional jump|Syscall param" "${log_file}"; then
    echo "FAIL: memory access/uninitialized issue reported in ${log_name}"
    grep -nE "Invalid read|Invalid write|Use of uninitialised|Conditional jump|Syscall param" "${log_file}" | head -n 10 || true
    failed=1
  fi

  if grep -Eq "definitely lost: [1-9]" "${log_file}"; then
    echo "FAIL: definitely lost bytes in ${log_name}"
    grep -n "definitely lost" "${log_file}" || true
    failed=1
  fi

  if grep -Eq "indirectly lost: [1-9]" "${log_file}"; then
    echo "FAIL: indirectly lost bytes in ${log_name}"
    grep -n "indirectly lost" "${log_file}" || true
    failed=1
  fi

done

echo "Valgrind summaries:"
for log_name in "${required_logs[@]}"; do
  log_file="${LOG_DIR}/${log_name}"
  if [[ -s "${log_file}" ]]; then
    echo "--- ${log_name} ---"
    grep -E "ERROR SUMMARY|definitely lost|indirectly lost|possibly lost|still reachable" "${log_file}" || true
  fi
done

if [[ "${failed}" -ne 0 ]]; then
  exit 1
fi

echo "PASS: CLI valgrind test passed (with ncurses/system allocations tolerated)."
