# Rock Paper Scissors Battle Royale (TCP + select)

A multiplayer client-server game for CSC209 Category 2.

One server process runs the match. Multiple terminal clients connect over TCP sockets. The server is authoritative: it owns game state, validates commands, runs simulation ticks, and broadcasts updates.

## What This Project Demonstrates

- TCP client-server communication
- One-process concurrency with select()
- Non-blocking sockets
- Buffered line-based protocol framing
- Robust handling of disconnects and slow clients

## Requirements

- Linux or macOS (POSIX sockets)
- gcc
- make
- ncurses
- tmux

## Build

Build server + terminal client:

```bash
make
```

Build only CLI targets:

```bash
make cli
```

## Run

Start the full tmux demo session:

```bash
./scripts/run_5_cli_clients.sh
```

This script:

- stops old tmux/process state for this repo
- rebuilds the project
- starts one server
- opens four client panes
- attaches to the tmux session

Manual run (separate terminals):

```bash
./build/bin/server
./build/bin/client_text alice
./build/bin/client_text bob
```

## tmux Controls You Need

Inside the demo session:

- Ctrl+b then o: move to the next pane (most important while presenting)
- Ctrl+b then Arrow key: move to pane in that direction
- Ctrl+b then n / p: next or previous window
- Ctrl+b then d: detach from tmux session

## Testing

Unit tests:

```bash
make test
```

Integration tests:

```bash
make integration-test
```

All tests:

```bash
make test-all
```

## Where The Major Socket/select Code Is

Start here if you want to understand networking quickly:

- src/server.c
  - create_listen_socket: creates listening TCP socket
  - build_select_sets: prepares read/write fd sets
  - main: select() loop, accept, read, write, and timer progression
  - accept_new_clients, process_player_reads, process_player_writes: core socket I/O flow

- src/protocol.c
  - connect_to_server: client TCP connect
  - send_line: newline-delimited send helper
  - queue_line + flush_player_output: buffered non-blocking server writes
  - read_into_player_buffer + pop_line: recv buffering and message framing

- src/server_commands.c
  - handle_command: parses one client command line and dispatches
  - add_player: allocates a slot for a newly accepted socket

- src/server_state.c
  - state transitions and server broadcasts
  - advance_match_timers: lobby/setup timers and chase tick progression

- src/client_network.c
  - pump_client_network: client-side non-blocking receive loop (uses select + recv helpers)

## Protocol Summary

Client -> Server commands:

- HELLO <name>
- CHOICE <R|P|S>
- SPAWN <x> <y>
- GET_STATE
- REPICK <R|P|S>
- REMATCH
- QUIT

Server -> Client messages include:

- INFO, WELCOME, ERROR
- LOBBY_OPEN / LOBBY_CLOSED / SETUP_OPEN
- JOINED, LEFT, ROUND_START
- REPICK_START / REPICK_DONE
- STATE_BEGIN ... PLAYER ... STATE_END
- GAME_OVER

## Notes

- The gameplay is terminal-first in this branch (no GUI requirement).
- Default port is 4242. You can override with PORT when running scripts or make commands.