#ifndef CLIENT_GUI_STATE_H
#define CLIENT_GUI_STATE_H

#include "common.h"

typedef struct
{
	int used;
	char name[MAX_NAME];
	char choice;
	int x;
	int y;
	int alive;
	int waiting;
} GuiPlayer;

typedef struct
{
	GuiPlayer players[MAX_PLAYERS];
	char status_text[256];
	char name_input[MAX_NAME];
	char pending_name[MAX_NAME];
	char my_name[MAX_NAME];
	char winner_name[MAX_NAME];
	int name_registered;
	int name_check_pending;
	int state_request_sent;
	int name_box_active;
	int repick_phase;
	int game_over;
	int can_attempt_join;
	int joined_match;
	int choice_confirmed;
	int spawn_confirmed;
	char selected_choice;
	double lobby_end_time;
	double setup_end_time;
	double round_end_time;
} GuiState;

void init_gui_state(GuiState *state, const char *initial_name);

/*
 * Client message ingress:
 * - pump_network reads from socket, extracts complete lines, and forwards each line.
 * - handle_gui_server_line parses one server message and updates GuiState.
 */
void handle_gui_server_line(GuiState *state, const char *line);
void pump_network(Player *net_player, GuiState *state);

#endif