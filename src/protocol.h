#ifndef PROTOCOL_H
#define PROTOCOL_H

#include "common.h"

/*
 * Shared line-based socket helpers used by both client and server.
 *
 * Message framing contract:
 * - send_line/queue_line append a trailing '\n'.
 * - read_into_player_buffer accumulates raw bytes from recv().
 * - pop_line extracts one complete newline-delimited message.
 */

/* Client connection helper. */
socket_t connect_to_server(const char *host, int port);

/* Direct socket write helper (client uses this heavily). */
int send_line(socket_t fd, const char *fmt, ...);

/* Buffered write helpers (server uses these for nonblocking output). */
int queue_line(Player *p, const char *fmt, ...);
int player_has_pending_output(const Player *p);
int flush_player_output(Player *p);

/* Buffered read + line extraction helpers. */
int read_into_player_buffer(Player *p);
int pop_line(Player *p, char *out, size_t out_sz);

#endif