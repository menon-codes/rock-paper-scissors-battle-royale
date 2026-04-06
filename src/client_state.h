#ifndef CLIENT_STATE_H
#define CLIENT_STATE_H

#include "common.h"

/*
 * Client-side projection of server state.
 *
 * ClientPlayer and ClientState intentionally mirror only fields needed for rendering
 * and input flow; they are not authoritative server truth.
 */

typedef struct
{
	/* Slot occupied by a known player from latest snapshot. */
	int used;
	char name[MAX_NAME_LENGTH];

	/* Last known R/P/S type and board position (float-based coordinates). */
	char choice;
	float x;
	float y;

	/* Admission/round visibility flags from STATE snapshot. */
	int alive;
	int waiting;
} ClientPlayer;

typedef struct
{
	/* Latest STATE snapshot players. */
	ClientPlayer players[MAX_PLAYERS];

	/* UI text and local input buffers. */
	char status_text[256];
	char name_input[MAX_NAME_LENGTH];
	char pending_name[MAX_NAME_LENGTH];
	char my_name[MAX_NAME_LENGTH];
	int match_result; /* 1 = won, -1 = lost, 0 = unknown/not finished */

	/* Registration and snapshot synchronization flags. */
	int name_registered;
	int name_check_pending;
	int state_request_sent;
	int name_box_active;

	/* Match participation and choice flow flags. */
	int repick_phase;
	int game_over;
	int can_attempt_join;
	int joined_match;
	int choice_confirmed;
	int spawn_confirmed;
	char selected_choice;

	/* Client wall-clock deadlines used for countdown display. */
	double lobby_end_time;
	double setup_end_time;
	double round_end_time;
} ClientState;

/* Initialize client state for a fresh client session. */
void init_client_state(ClientState *state, const char *initial_name);

/*
 * Client message ingress:
 * - pump_client_network reads from socket, extracts complete lines, and forwards each line.
 * - handle_server_line parses one server message and updates ClientState.
 */
void handle_server_line(ClientState *state, const char *line);

/* Non-blocking network pump; drains all currently available server lines. */
void pump_client_network(Player *net_player, ClientState *state);

#endif