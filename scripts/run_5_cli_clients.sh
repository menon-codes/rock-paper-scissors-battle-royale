#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
SESSION_NAME="rps-cli"
PORT="${PORT:-4242}"
PORT_STAMP="build/.last_cli_port"

if ! command -v tmux >/dev/null 2>&1; then
  echo "tmux is required but was not found in PATH." >&2
  exit 1
fi

cd "${REPO_ROOT}"

kill_repo_binary_pids() {
  local exe_path="$1"
  local proc_name
  proc_name="$(basename "${exe_path}")"

  while IFS= read -r pid; do
    [[ -n "${pid}" ]] || continue
    local resolved
    resolved="$(readlink -f "/proc/${pid}/exe" 2>/dev/null || true)"
    if [[ "${resolved}" == "${exe_path}" ]]; then
      kill "${pid}" 2>/dev/null || true
    fi
  done < <(pgrep -x "${proc_name}" 2>/dev/null || true)
}

port_in_use() {
  if command -v ss >/dev/null 2>&1; then
    ss -ltn "sport = :${PORT}" 2>/dev/null | awk 'NR > 1 {found = 1} END {exit found ? 0 : 1}'
  else
    return 1
  fi
}

echo "Stopping old session/processes..."
tmux kill-session -t "${SESSION_NAME}" 2>/dev/null || true

# Kill only binaries that resolve to this repository to avoid collateral kills.
kill_repo_binary_pids "${REPO_ROOT}/build/bin/server"
kill_repo_binary_pids "${REPO_ROOT}/build/bin/client_text"

# Fallback for relative invocations from this repo shell.
pkill -f "(^|[[:space:]])\./build/bin/server([[:space:]]|$)" 2>/dev/null || true
pkill -f "(^|[[:space:]])\./build/bin/client_text([[:space:]]|$)" 2>/dev/null || true

# If a stale listener still owns the fixed game port, terminate it.
if command -v ss >/dev/null 2>&1; then
  while IFS= read -r pid; do
    [[ -n "${pid}" ]] || continue
    kill "${pid}" 2>/dev/null || true
  done < <(
    ss -ltnp "sport = :${PORT}" 2>/dev/null \
      | awk -F'pid=' '/pid=/{split($2,a,","); print a[1]}' \
      | sort -u
  )
fi

if port_in_use; then
  echo "Port ${PORT} is still busy after cleanup." >&2
  echo "Another process may own it. Try a different port, for example:" >&2
  echo "  PORT=4343 ./scripts/run_4_cli_clients.sh" >&2
  exit 1
fi

echo "Building project..."
if [[ ! -f "${PORT_STAMP}" ]] || [[ "$(<"${PORT_STAMP}")" != "${PORT}" ]]; then
  echo "Port macro changed or unknown build cache; rebuilding binaries for PORT=${PORT}."
  make clean >/dev/null
fi
make -j4 PORT="${PORT}"
echo "${PORT}" >"${PORT_STAMP}"

echo "Starting tmux session: ${SESSION_NAME}"
tmux new-session -d -s "${SESSION_NAME}" -n backend "./build/bin/server"
sleep 1

if ! tmux has-session -t "${SESSION_NAME}" 2>/dev/null; then
  echo "Backend failed to start; tmux session closed immediately." >&2
  echo "Re-running server once to show the startup error:" >&2
  ./build/bin/server || true
  exit 1
fi

tmux set-option -t "${SESSION_NAME}" automatic-rename off
tmux set-option -t "${SESSION_NAME}" allow-rename off

tmux new-window -d -t "${SESSION_NAME}:1" -n players "./build/bin/client_text alice"
tmux split-window -h -t "${SESSION_NAME}:players.0" "./build/bin/client_text bob"
tmux split-window -v -t "${SESSION_NAME}:players.0" "./build/bin/client_text Cootsi"
tmux split-window -v -t "${SESSION_NAME}:players.1" "./build/bin/client_text dan"
tmux select-layout -t "${SESSION_NAME}:players" tiled

tmux set-option -t "${SESSION_NAME}:players" pane-border-status top
tmux select-pane -t "${SESSION_NAME}:players.0" -T client-1
tmux select-pane -t "${SESSION_NAME}:players.1" -T client-2
tmux select-pane -t "${SESSION_NAME}:players.2" -T client-3
tmux select-pane -t "${SESSION_NAME}:players.3" -T client-4

tmux select-window -t "${SESSION_NAME}:players"
tmux select-pane -t "${SESSION_NAME}:players.0"

echo "Attaching to tmux session."
echo "Showing 4 players in one tiled layout."
echo "Server is running in the hidden 'backend' window."
echo "Default client names are: alice, bob, Cootsi, dan."
echo "Pick type with R/P/S, move with arrows, and press Space to send spawn."
echo "Window switch: Ctrl+b then n (next), p (prev), w (list), 0/1 (jump)."
echo "Pane switch:   Ctrl+b then o (cycle) or Arrow keys (direction)."
tmux attach -t "${SESSION_NAME}"
