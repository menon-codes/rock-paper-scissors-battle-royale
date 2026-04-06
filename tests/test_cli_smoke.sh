#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
SESSION_NAME="rps-cli-smoke-$$"

cleanup() {
  tmux kill-session -t "${SESSION_NAME}" 2>/dev/null || true
  pkill -f "/build/bin/server" 2>/dev/null || true
  pkill -f "/build/bin/client_text" 2>/dev/null || true
}
trap cleanup EXIT

if ! command -v tmux >/dev/null 2>&1; then
  echo "SKIP: tmux not found; skipping cli smoke test."
  exit 0
fi

cd "${REPO_ROOT}"
pkill -f "/build/bin/server" 2>/dev/null || true
pkill -f "/build/bin/client_text" 2>/dev/null || true

echo "Starting CLI smoke test in tmux session ${SESSION_NAME}"

tmux new-session -d -s "${SESSION_NAME}" -n backend

tmux set-option -t "${SESSION_NAME}" automatic-rename off
tmux set-option -t "${SESSION_NAME}" allow-rename off

tmux send-keys -t "${SESSION_NAME}:backend.0" "./build/bin/server" C-m
sleep 1

if ! pgrep -f "./build/bin/server" >/dev/null 2>&1; then
  echo "FAIL: server did not stay running in smoke test."
  tmux capture-pane -p -t "${SESSION_NAME}:backend.0" | tail -n 30 || true
  exit 1
fi

tmux new-window -d -t "${SESSION_NAME}:1" -n players
tmux split-window -h -t "${SESSION_NAME}:1.0"
tmux split-window -v -t "${SESSION_NAME}:1.0"
tmux split-window -v -t "${SESSION_NAME}:1.1"
tmux select-layout -t "${SESSION_NAME}:1" tiled

tmux send-keys -t "${SESSION_NAME}:1.0" "./build/bin/client_text alpha R 1 1" C-m
tmux send-keys -t "${SESSION_NAME}:1.1" "./build/bin/client_text beta P 2 2" C-m
tmux send-keys -t "${SESSION_NAME}:1.2" "./build/bin/client_text gamma S 3 3" C-m
tmux send-keys -t "${SESSION_NAME}:1.3" "./build/bin/client_text delta R 4 4" C-m

sleep 6

for pattern in \
  "./build/bin/client_text alpha R 1 1" \
  "./build/bin/client_text beta P 2 2" \
  "./build/bin/client_text gamma S 3 3" \
  "./build/bin/client_text delta R 4 4"
do
  if ! pgrep -f "${pattern}" >/dev/null 2>&1; then
    echo "FAIL: expected running CLI client missing: ${pattern}"
    exit 1
  fi
done

for pane in 0 1 2 3; do
  tmux send-keys -t "${SESSION_NAME}:1.${pane}" q
done

all_clients_exited=0
for _ in $(seq 1 50); do
  if ! pgrep -f "/build/bin/client_text" >/dev/null 2>&1; then
    all_clients_exited=1
    break
  fi
  sleep 0.2
done

if [[ "${all_clients_exited}" -ne 1 ]]; then
  echo "FAIL: timed out waiting for CLI clients to quit."
  exit 1
fi

tmux send-keys -t "${SESSION_NAME}:backend.0" C-c
sleep 1

echo "PASS: CLI smoke test completed successfully."
