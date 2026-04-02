#ifndef SERVER_COMMANDS_H
#define SERVER_COMMANDS_H

#include "common.h"

/*
 * Server command ingress:
 * - add_player is called from server.c when accept() succeeds.
 * - handle_command is called once per parsed input line.
 */

/* Allocate and initialize a player slot for a newly accepted socket. */
int add_player(ServerState *s, socket_t fd);

/* Parse and dispatch one client command line for the player at index idx. */
void handle_command(ServerState *s, int idx, const char *line);

#endif