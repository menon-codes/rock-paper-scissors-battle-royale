#ifndef CLIENT_GUI_STATE_H
#define CLIENT_GUI_STATE_H

#include "common.h"

/*
 * GUI-side projection of server state.
 *
 * GuiPlayer and GuiState intentionally mirror only fields needed for rendering
 * and input flow; they are not authoritative server truth.
 */

typedef struct
{
	/* Slot occupied by a known player from latest snapshot. */
	int used;
	char name[MAX_NAME];

	/* Last known R/P/S type and board position (float-based coordinates). */
	char choice;
	float x;
	float y;

	/* Admission/round visibility flags from STATE snapshot. */
	int alive;
	int waiting;
} GuiPlayer;

typedef struct
{
	/* Latest STATE snapshot players. */
	GuiPlayer players[MAX_PLAYERS];

	/* UI text and local input buffers. */
	char status_text[256];
	char name_input[MAX_NAME];
	char pending_name[MAX_NAME];
	char my_name[MAX_NAME];
	char winner_name[MAX_NAME];

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
} GuiState;

/* Initialize GUI state for a fresh client session. */
void init_gui_state(GuiState *state, const char *initial_name);

/*
 * Client message ingress:
 * - pump_network reads from socket, extracts complete lines, and forwards each line.
 * - handle_gui_server_line parses one server message and updates GuiState.
 */
void handle_gui_server_line(GuiState *state, const char *line);

/* Non-blocking network pump; drains all currently available server lines. */
void pump_network(Player *net_player, GuiState *state);

#endif