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

/*
 * Direct socket write helper (client uses this heavily).
 * Returns 0 on success, -1 on socket or formatting failure.
 */
int send_line(socket_t fd, const char *fmt, ...);

/*
 * Buffered write helpers (server uses these for nonblocking output).
 * queue_line returns 0 on success, -1 if the formatted line does not fit.
 * player_has_pending_output returns non-zero when buffered bytes are waiting.
 * flush_player_output returns:
 *   1 when all buffered bytes were flushed,
 *   0 when send would block and bytes remain,
 *  -1 on unrecoverable socket error.
 */
int queue_line(Player *p, const char *fmt, ...);
int player_has_pending_output(const Player *p);
int flush_player_output(Player *p);

/*
 * Buffered read + line extraction helpers.
 * read_into_player_buffer returns:
 *   >0 bytes read,
 *    1 on would-block (no new bytes),
 *    0 on orderly disconnect,
 *   -1 on error or overflow.
 * pop_line returns 1 when a full line was extracted, otherwise 0.
 */
int read_into_player_buffer(Player *p);
int pop_line(Player *p, char *out, size_t out_sz);

#endif