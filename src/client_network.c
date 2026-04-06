#include "client_state.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "protocol.h"

/*
 * Client networking glue:
 * - initializes local UI state
 * - performs non-blocking socket draining each frame
 */

void init_client_state(ClientState *state, const char *initial_name)
{
	memset(state, 0, sizeof(*state));
	snprintf(state->name_input, sizeof(state->name_input), "%s", initial_name);
	snprintf(state->status_text, sizeof(state->status_text), "Connected");
}

void pump_client_network(Player *net_player, ClientState *state)
{
	/* Drain all currently ready data so rendering sees the newest state this frame. */
	while (1)
	{
		fd_set readfds;
		FD_ZERO(&readfds);
		FD_SET(net_player->fd, &readfds);

		struct timeval tv;
		tv.tv_sec = 0;
		tv.tv_usec = 0;

		int ready = select(net_player->fd + 1, &readfds, NULL, NULL, &tv);
		if (ready < 0)
		{
			if (NET_INTERRUPTED(NET_LAST_ERROR()))
				continue;
			break;
		}
		if (ready == 0)
		{
			break;
		}

		int rc = read_into_player_buffer(net_player);
		if (rc <= 0)
		{
			snprintf(state->status_text, sizeof(state->status_text), "Disconnected from server");
			state->game_over = 1;
			break;
		}

		char line[MAX_LINE];
		while (pop_line(net_player, line, sizeof(line)))
		{
			handle_server_line(state, line);
		}
	}
}