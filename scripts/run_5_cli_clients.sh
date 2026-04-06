#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
SESSION_NAME="rps-cli"

if ! command -v tmux >/dev/null 2>&1; then
  echo "tmux is required but was not found in PATH." >&2
  exit 1
fi

cd "${REPO_ROOT}"

echo "Stopping old session/processes..."
tmux kill-session -t "${SESSION_NAME}" 2>/dev/null || true
pkill -f "/build/bin/server" 2>/dev/null || true
pkill -f "/build/bin/client_text" 2>/dev/null || true

echo "Building CLI targets (server + client_text)..."
make -j4 cli

echo "Starting tmux session: ${SESSION_NAME}"
tmux new-session -d -s "${SESSION_NAME}" -n server "./build/bin/server"
sleep 1

tmux new-window -t "${SESSION_NAME}:1" -n p1 "./build/bin/client_text alice R 1 1"
tmux new-window -t "${SESSION_NAME}:2" -n p2 "./build/bin/client_text bob P 8 1"
tmux new-window -t "${SESSION_NAME}:3" -n p3 "./build/bin/client_text carol S 1 8"
tmux new-window -t "${SESSION_NAME}:4" -n p4 "./build/bin/client_text dave R 8 8"
tmux new-window -t "${SESSION_NAME}:5" -n watch "./build/bin/client_text spectator"

echo "Attaching to tmux session."
echo "Switch windows with Ctrl+b then n/p, number, or w."
tmux attach -t "${SESSION_NAME}"
