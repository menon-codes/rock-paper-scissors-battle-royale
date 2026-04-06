# Networking Map

This file is the quick index for socket and message flow.

## Where Socket Code Lives

- Shared socket + line framing helpers: src/protocol.h and src/protocol.c
- Server socket loop: src/server.c
- Server command parsing (one inbound line at a time): src/server_commands.c
- Server state machine + outbound broadcasts: src/server_state.c
- Client socket pump: src/client_network.c
- Client line parser (one inbound line at a time): src/client_text_messages.c
- Text client interactive loop: src/client_text.c

## Core Protocol Contract

- Transport is TCP with newline-delimited messages.
- Every outbound message is formed with send_line or queue_line and ends with a newline.
- Inbound bytes are buffered with read_into_player_buffer.
- Complete lines are extracted with pop_line.

This makes parsing deterministic: handlers only receive complete logical lines.

## Server Runtime Flow

1. src/server.c creates listen socket, sets nonblocking mode, and enters select loop.
2. On accept(), src/server.c calls add_player in src/server_commands.c.
3. For readable client sockets, src/server.c calls read_into_player_buffer and pop_line.
4. Each parsed line is sent to handle_command in src/server_commands.c.
5. Command handlers call state operations in src/server_state.c.
6. State operations enqueue output via queue_line.
7. Back in src/server.c, writable sockets are flushed via flush_player_output.
8. Timer checks in src/server.c call close_lobby_if_needed, expire_unready_setup_players, and resolve_round.

## Client Runtime Flow

1. src/client_text.c creates socket via connect_to_server and sends initial GET_STATE.
2. Per frame, src/client_text.c calls pump_client_network in src/client_network.c.
3. pump_network reads bytes and extracts lines (read_into_player_buffer + pop_line).
4. Each line is routed to handle_server_line in src/client_text_messages.c.
5. Parser updates ClientState (status text, player list, phase flags, timers).
6. src/client_text.c renders directly from ClientState.

## Text Client Runtime Flow

- src/client_text.c connects with connect_to_server.
- Sends user commands with send_line.
- Prints server lines extracted from pop_line.

## Message Ownership Guide

- Server accepts and routes client commands: src/server_commands.c
- Server decides game phase transitions and broadcasts: src/server_state.c
- Client interprets server messages into local UI state: src/client_text_messages.c
- Shared wire helpers and framing live only in src/protocol.c

## How To Trace A Single Message

Example: HELLO name

1. Client sends HELLO using send_line.
2. Server reads and pop_line extracts HELLO.
3. handle_command routes to handle_hello_command.
4. Handler queues responses (WELCOME, SPECTATING, etc.) using queue_line.
5. Server flushes queued bytes on writable event.
6. Client pump_network reads lines.
7. handle_server_line updates ClientState fields shown in UI.

If you start debugging protocol behavior, begin at src/protocol.c, then follow the per-line handlers in src/server_commands.c and src/client_text_messages.c.
