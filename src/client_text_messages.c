#include "client_gui_state.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Terminal-client message parser.
 *
 * This mirrors GUI parsing behavior without depending on raylib symbols,
 * so client_text can evolve independently of client_gui.
 */

typedef void (*LineParser)(GuiState *state, const char *line);

typedef struct
{
	const char *token;
	int exact;
	LineParser parser;
} GuiDispatchEntry;

static int find_player(GuiPlayer players[], const char *name)
{
	for (int i = 0; i < MAX_PLAYERS; i++)
	{
		if (players[i].used && strcmp(players[i].name, name) == 0)
		{
			return i;
		}
	}
	return -1;
}

static int get_or_add_player(GuiPlayer players[], const char *name)
{
	int idx = find_player(players, name);
	if (idx != -1)
		return idx;

	for (int i = 0; i < MAX_PLAYERS; i++)
	{
		if (!players[i].used)
		{
			memset(&players[i], 0, sizeof(players[i]));
			players[i].used = 1;
			snprintf(players[i].name, sizeof(players[i].name), "%s", name);
			players[i].x = -1;
			players[i].y = -1;
			players[i].choice = '?';
			return i;
		}
	}
	return -1;
}

static void clear_gui_players(GuiPlayer players[])
{
	memset(players, 0, sizeof(GuiPlayer) * MAX_PLAYERS);
}

static void parse_state_snapshot_line(GuiState *state, const char *line)
{
	char name1[MAX_NAME_LENGTH], name2[MAX_NAME_LENGTH], winner[MAX_NAME_LENGTH];
	char choice;
	float x, y;
	int alive, waiting;
	int round_no, seconds;

	if (strcmp(line, "STATE_BEGIN") == 0)
	{
		clear_gui_players(state->players);
		return;
	}

	if (strcmp(line, "STATE_END") == 0)
	{
		return;
	}

	if (sscanf(line, "PLAYER %31s %c %f %f %d %d",
			   name1, &choice, &x, &y, &alive, &waiting) == 6)
	{
		int idx = get_or_add_player(state->players, name1);
		if (idx != -1)
		{
			state->players[idx].choice = choice;
			state->players[idx].x = x;
			state->players[idx].y = y;
			state->players[idx].alive = alive;
			state->players[idx].waiting = waiting;
		}

		if (strcmp(name1, state->my_name) == 0)
		{
			if (choice == 'R' || choice == 'P' || choice == 'S')
			{
				state->selected_choice = choice;
			}
		}
		return;
	}

	if (sscanf(line, "ROUND_START %d %d", &round_no, &seconds) == 2)
	{
		state->repick_phase = 0;
		state->round_end_time = (seconds > 0) ? (rps_now_seconds() + seconds) : 0.0;
		state->setup_end_time = 0.0;
		snprintf(state->status_text, sizeof(state->status_text), "Match started");
		return;
	}

	if (strcmp(line, "REPICK_START") == 0)
	{
		state->repick_phase = 1;
		snprintf(state->status_text, sizeof(state->status_text), "Repick phase. Press R, P, or S.");
		return;
	}

	if (strcmp(line, "REPICK_DONE") == 0)
	{
		state->repick_phase = 0;
		snprintf(state->status_text, sizeof(state->status_text), "Repick finished");
		return;
	}

	if (strncmp(line, "PAIR ", 5) == 0)
	{
		char c1, c2;
		float move_x, move_y;

		if (sscanf(line, "PAIR %31s %31s %c %c WINNER %31s MOVE %f %f",
				   name1, name2, &c1, &c2, winner, &move_x, &move_y) == 7)
		{
			int idx_w = find_player(state->players, winner);
			const char *loser = (strcmp(winner, name1) == 0) ? name2 : name1;
			int idx_l = find_player(state->players, loser);

			if (idx_w != -1)
			{
				state->players[idx_w].x = move_x;
				state->players[idx_w].y = move_y;
				state->players[idx_w].alive = 1;
				state->players[idx_w].waiting = 0;
			}
			if (idx_l != -1)
			{
				state->players[idx_l].alive = 0;
				state->players[idx_l].waiting = 0;
				state->players[idx_l].x = -1.0f;
				state->players[idx_l].y = -1.0f;
			}

			snprintf(state->status_text, sizeof(state->status_text), "%s beat %s", winner, loser);
			return;
		}

		if (sscanf(line, "PAIR %31s %31s %c %c TIE", name1, name2, &c1, &c2) == 4)
		{
			snprintf(state->status_text, sizeof(state->status_text), "%s and %s tied", name1, name2);
			return;
		}
	}

	if (sscanf(line, "BYE %31s", name1) == 1)
	{
		snprintf(state->status_text, sizeof(state->status_text), "%s got a bye", name1);
		return;
	}

	if (sscanf(line, "GAME_OVER %31s", name1) == 1)
	{
		state->game_over = 1;
		state->round_end_time = 0.0;

		if (strcmp(name1, "WIN") == 0)
		{
			state->match_result = 1;
			snprintf(state->status_text, sizeof(state->status_text), "You won. Press M for rematch.");
		}
		else if (strcmp(name1, "LOSE") == 0)
		{
			state->match_result = -1;
			snprintf(state->status_text, sizeof(state->status_text), "You lost. Press M for rematch.");
		}
		else
		{
			state->match_result = 0;
			snprintf(state->status_text, sizeof(state->status_text), "Game over. Press M for rematch.");
		}
		return;
	}
}

static void parse_general_line(GuiState *state, const char *line)
{
	char name1[MAX_NAME_LENGTH];
	char choice;
	float x, y;
	long sec_long;
	int setup_seconds;
	int id;

	if (sscanf(line, "WELCOME %d", &id) == 1)
	{
		if (state->pending_name[0] != '\0')
		{
			snprintf(state->my_name, sizeof(state->my_name), "%s", state->pending_name);
		}
		state->name_registered = 1;
		state->name_check_pending = 0;
		snprintf(state->status_text, sizeof(state->status_text), "Registered as %s", state->my_name[0] ? state->my_name : state->pending_name);
		return;
	}

	if (strcmp(line, "MATCH_RESET") == 0)
	{
		clear_gui_players(state->players);
		state->repick_phase = 0;
		state->game_over = 0;
		state->can_attempt_join = 1;
		state->joined_match = 0;
		state->choice_confirmed = 0;
		state->spawn_confirmed = 0;
		state->selected_choice = 0;
		state->lobby_end_time = 0.0;
		state->setup_end_time = 0.0;
		state->round_end_time = 0.0;
		state->match_result = 0;
		snprintf(state->status_text, sizeof(state->status_text), "Match reset. You are spectating again.");
		return;
	}

	if (strcmp(line, "SPECTATING") == 0)
	{
		snprintf(state->status_text, sizeof(state->status_text), "Spectating. Pick a type and click a tile if lobby is open.");
		return;
	}

	if (strcmp(line, "LOBBY_WAITING") == 0)
	{
		state->can_attempt_join = 1;
		state->lobby_end_time = 0.0;
		snprintf(state->status_text, sizeof(state->status_text), "Lobby idle. Be the first player to join.");
		return;
	}

	if (sscanf(line, "LOBBY_OPEN %ld", &sec_long) == 1)
	{
		state->can_attempt_join = 1;
		state->lobby_end_time = rps_now_seconds() + sec_long;
		snprintf(state->status_text, sizeof(state->status_text), "Lobby open. Choose type and click a spawn tile.");
		return;
	}

	if (strcmp(line, "LOBBY_CLOSED") == 0)
	{
		state->can_attempt_join = 0;
		state->lobby_end_time = 0.0;
		snprintf(state->status_text, sizeof(state->status_text), "Lobby closed. Spectating only.");
		return;
	}

	if (sscanf(line, "SETUP_OPEN %d", &setup_seconds) == 1)
	{
		state->setup_end_time = rps_now_seconds() + setup_seconds;
		snprintf(state->status_text, sizeof(state->status_text), "Setup locked. Waiting for admitted players.");
		return;
	}

	if (strcmp(line, "CHOOSE_TYPE") == 0 || strncmp(line, "CHOOSE_SPAWN", 12) == 0)
	{
		state->can_attempt_join = 1;
		return;
	}

	if (sscanf(line, "CHOICE_OK %c", &choice) == 1)
	{
		state->selected_choice = choice;
		state->choice_confirmed = 1;
		snprintf(state->status_text, sizeof(state->status_text), "Choice confirmed: %c", choice);
		return;
	}

	if (sscanf(line, "SPAWN_OK %f %f", &x, &y) == 2)
	{
		state->spawn_confirmed = 1;
		snprintf(state->status_text, sizeof(state->status_text), "Spawn confirmed at (%.1f,%.1f)", x, y);
		return;
	}

	if (strcmp(line, "JOINED_MATCH") == 0)
	{
		state->joined_match = 1;
		state->can_attempt_join = 0;
		snprintf(state->status_text, sizeof(state->status_text), "You joined the current match.");
		return;
	}

	if (strncmp(line, "WAITING_FOR_OTHERS", 18) == 0)
	{
		snprintf(state->status_text, sizeof(state->status_text), "Waiting for other admitted players.");
		return;
	}

	if (strncmp(line, "ERROR spawn_taken", 17) == 0)
	{
		state->spawn_confirmed = 0;
		snprintf(state->status_text, sizeof(state->status_text), "Spawn taken. Click another tile.");
		return;
	}

	if (strncmp(line, "ERROR lobby_closed", 18) == 0)
	{
		state->can_attempt_join = 0;
		snprintf(state->status_text, sizeof(state->status_text), "Too late to join this match. Spectating only.");
		return;
	}

	if (strncmp(line, "ERROR already_joined", 20) == 0)
	{
		snprintf(state->status_text, sizeof(state->status_text), "Already joined this match.");
		return;
	}

	if (strncmp(line, "ERROR rematch_not_available", 27) == 0)
	{
		snprintf(state->status_text, sizeof(state->status_text), "Rematch only works after GAME_OVER.");
		return;
	}

	if (strncmp(line, "ERROR duplicate_name", 20) == 0)
	{
		state->name_check_pending = 0;
		state->name_registered = 0;
		snprintf(state->status_text, sizeof(state->status_text), "Name is taken. Try another name.");
		return;
	}

	if (strncmp(line, "ERROR already_registered", 24) == 0)
	{
		state->name_check_pending = 0;
		state->name_registered = 1;
		snprintf(state->status_text, sizeof(state->status_text), "Name already registered on this connection.");
		return;
	}

	if (strncmp(line, "ERROR register_first", 20) == 0)
	{
		state->name_registered = 0;
		snprintf(state->status_text, sizeof(state->status_text), "Set your name first.");
		return;
	}

	if (strncmp(line, "JOINED ", 7) == 0)
	{
		if (sscanf(line, "JOINED %31s", name1) == 1)
		{
			snprintf(state->status_text, sizeof(state->status_text), "%s joined the match", name1);
		}
		return;
	}

	if (sscanf(line, "LEFT %31s", name1) == 1)
	{
		int idx = find_player(state->players, name1);
		if (idx != -1)
		{
			state->players[idx].used = 0;
		}
		snprintf(state->status_text, sizeof(state->status_text), "%s left", name1);
		return;
	}

	if (strncmp(line, "ERROR ", 6) == 0)
	{
		snprintf(state->status_text, sizeof(state->status_text), "%s", line);
		return;
	}
}

static int line_matches(const GuiDispatchEntry *entry, const char *line)
{
	if (entry->exact)
	{
		return strcmp(line, entry->token) == 0;
	}
	return strncmp(line, entry->token, strlen(entry->token)) == 0;
}

void handle_gui_server_line(GuiState *state, const char *line)
{
	static const GuiDispatchEntry dispatch[] = {
		{"WELCOME ", 0, parse_general_line},
		{"MATCH_RESET", 1, parse_general_line},
		{"SPECTATING", 1, parse_general_line},
		{"LOBBY_WAITING", 1, parse_general_line},
		{"LOBBY_OPEN ", 0, parse_general_line},
		{"LOBBY_CLOSED", 1, parse_general_line},
		{"SETUP_OPEN ", 0, parse_general_line},
		{"CHOOSE_TYPE", 1, parse_general_line},
		{"CHOOSE_SPAWN", 0, parse_general_line},
		{"CHOICE_OK ", 0, parse_general_line},
		{"SPAWN_OK ", 0, parse_general_line},
		{"JOINED_MATCH", 1, parse_general_line},
		{"WAITING_FOR_OTHERS", 0, parse_general_line},
		{"JOINED ", 0, parse_general_line},
		{"LEFT ", 0, parse_general_line},
		{"ERROR ", 0, parse_general_line},

		{"STATE_BEGIN", 1, parse_state_snapshot_line},
		{"STATE_END", 1, parse_state_snapshot_line},
		{"PLAYER ", 0, parse_state_snapshot_line},
		{"ROUND_START ", 0, parse_state_snapshot_line},
		{"REPICK_START", 1, parse_state_snapshot_line},
		{"REPICK_DONE", 1, parse_state_snapshot_line},
		{"PAIR ", 0, parse_state_snapshot_line},
		{"BYE ", 0, parse_state_snapshot_line},
		{"GAME_OVER ", 0, parse_state_snapshot_line},
	};

	for (size_t i = 0; i < sizeof(dispatch) / sizeof(dispatch[0]); i++)
	{
		if (line_matches(&dispatch[i], line))
		{
			dispatch[i].parser(state, line);
			return;
		}
	}
}
