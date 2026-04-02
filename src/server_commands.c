#include "server_commands.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "protocol.h"
#include "server_state.h"

typedef void (*CommandHandler)(ServerState *s, int idx, const char *line);

typedef struct
{
	const char *token;
	int exact;
	CommandHandler handler;
} CommandDispatchEntry;

static int find_name(ServerState *s, const char *name)
{
	for (int i = 0; i < MAX_PLAYERS; i++)
	{
		Player *p = &s->players[i];
		if (player_is_registered(p) && strcmp(p->name, name) == 0)
		{
			return i;
		}
	}
	return -1;
}

static int spawn_taken(ServerState *s, int x, int y, int ignore_idx)
{
	for (int i = 0; i < MAX_PLAYERS; i++)
	{
		if (i == ignore_idx)
			continue;

		Player *p = &s->players[i];
		if (!player_is_admitted(p))
			continue;
		if (!p->spawn_chosen)
			continue;

		if (p->x == x && p->y == y)
		{
			return 1;
		}
	}
	return 0;
}

static void reset_player_progress(Player *p)
{
	p->admitted = 0;
	p->alive = 0;
	p->waiting = 0;
	p->in_round = 0;

	p->choice_chosen = 0;
	p->spawn_chosen = 0;

	p->repick_submitted = 0;
	p->repick_choice = 0;

	p->choice = 0;
	p->x = -1;
	p->y = -1;
}

static void init_player_slot(Player *p, socket_t fd, int id)
{
	memset(p, 0, sizeof(*p));
	p->fd = fd;
	p->id = id;
	p->connected = 1;
	p->x = -1;
	p->y = -1;
}

int add_player(ServerState *s, socket_t fd)
{
	for (int i = 0; i < MAX_PLAYERS; i++)
	{
		if (!s->players[i].connected)
		{
			init_player_slot(&s->players[i], fd, s->next_id++);
			return i;
		}
	}
	return -1;
}

static void handle_hello_command(ServerState *s, int idx, const char *line)
{
	Player *p = &s->players[idx];
	char name[MAX_NAME];

	if (p->registered)
	{
		(void)queue_line(p, "ERROR already_registered");
		return;
	}

	if (sscanf(line + 6, "%31s", name) != 1)
	{
		(void)queue_line(p, "ERROR usage_HELLO_name");
		return;
	}

	if (find_name(s, name) != -1)
	{
		(void)queue_line(p, "ERROR duplicate_name");
		return;
	}

	snprintf(p->name, sizeof(p->name), "%s", name);
	p->registered = 1;
	reset_player_progress(p);

	(void)queue_line(p, "WELCOME %d", p->id);
	(void)queue_line(p, "SPECTATING");

	if (s->phase == PHASE_LOBBY_OPEN)
	{
		(void)queue_line(p, "CHOOSE_TYPE");
		(void)queue_line(p, "CHOOSE_SPAWN %d %d", GRID_W, GRID_H);

		if (s->join_deadline == 0)
		{
			(void)queue_line(p, "LOBBY_WAITING");
		}
		else
		{
			(void)queue_line(p, "LOBBY_OPEN %ld", (long)(s->join_deadline - time(NULL) > 0 ? s->join_deadline - time(NULL) : 0));
		}
	}
	else
	{
		(void)queue_line(p, "LOBBY_CLOSED");
	}
}

static void handle_choice_command(ServerState *s, int idx, const char *line)
{
	Player *p = &s->players[idx];
	char choice = line[7];

	if (!p->registered)
	{
		(void)queue_line(p, "ERROR register_first");
		return;
	}

	if (p->admitted)
	{
		(void)queue_line(p, "ERROR already_joined");
		return;
	}

	if (choice >= 'a' && choice <= 'z')
	{
		choice = (char)(choice - 'a' + 'A');
	}

	if (choice != 'R' && choice != 'P' && choice != 'S')
	{
		(void)queue_line(p, "ERROR bad_choice");
		return;
	}

	p->choice = choice;
	p->choice_chosen = 1;
	(void)queue_line(p, "CHOICE_OK %c", choice);

	maybe_admit_player(s, idx);
}

static void handle_spawn_command(ServerState *s, int idx, const char *line)
{
	Player *p = &s->players[idx];
	int x, y;

	if (!p->registered)
	{
		(void)queue_line(p, "ERROR register_first");
		return;
	}

	if (p->admitted)
	{
		(void)queue_line(p, "ERROR already_joined");
		return;
	}

	if (sscanf(line + 6, "%d %d", &x, &y) != 2)
	{
		(void)queue_line(p, "ERROR usage_SPAWN_x_y");
		return;
	}

	if (x < 0 || x >= GRID_W || y < 0 || y >= GRID_H)
	{
		(void)queue_line(p, "ERROR bad_spawn");
		return;
	}

	if (spawn_taken(s, x, y, idx))
	{
		(void)queue_line(p, "ERROR spawn_taken");
		return;
	}

	p->x = x;
	p->y = y;
	p->spawn_chosen = 1;
	(void)queue_line(p, "SPAWN_OK %d %d", x, y);

	maybe_admit_player(s, idx);
}

static void handle_repick_command(ServerState *s, int idx, const char *line)
{
	Player *p = &s->players[idx];
	char choice = line[7];

	if (choice >= 'a' && choice <= 'z')
	{
		choice = (char)(choice - 'a' + 'A');
	}

	if (s->phase != PHASE_REPICK)
	{
		(void)queue_line(p, "ERROR not_in_repick_phase");
		return;
	}

	if (!p->registered || !p->admitted || !p->alive)
	{
		(void)queue_line(p, "ERROR not_alive");
		return;
	}

	if (p->repick_submitted)
	{
		(void)queue_line(p, "ERROR already_repicked");
		return;
	}

	if (choice != 'R' && choice != 'P' && choice != 'S')
	{
		(void)queue_line(p, "ERROR bad_choice");
		return;
	}

	p->repick_choice = choice;
	p->repick_submitted = 1;
	(void)queue_line(p, "REPICK_OK %c", choice);

	if (all_alive_repicked(s))
	{
		finish_repicks(s);
	}
	else
	{
		(void)queue_line(p, "REPICK_WAITING");
	}
}

static void handle_get_state_command(ServerState *s, int idx, const char *line)
{
	(void)line;
	queue_game_state_for_player(s, &s->players[idx]);
}

static void handle_rematch_command(ServerState *s, int idx, const char *line)
{
	(void)line;
	Player *p = &s->players[idx];
	if (s->phase != PHASE_GAME_OVER)
	{
		(void)queue_line(p, "ERROR rematch_not_available");
		return;
	}

	reset_match(s);
}

static void handle_quit_command(ServerState *s, int idx, const char *line)
{
	(void)line;
	drop_player(s, idx, 1);
	reevaluate_state(s);
}

static int command_matches(const CommandDispatchEntry *entry, const char *line)
{
	if (entry->exact)
	{
		return strcmp(line, entry->token) == 0;
	}
	return strncmp(line, entry->token, strlen(entry->token)) == 0;
}

void handle_command(ServerState *s, int idx, const char *line)
{
	Player *p = &s->players[idx];
	static const CommandDispatchEntry dispatch[] = {
		{"HELLO ", 0, handle_hello_command},
		{"GET_STATE", 1, handle_get_state_command},
		{"CHOICE ", 0, handle_choice_command},
		{"SPAWN ", 0, handle_spawn_command},
		{"REPICK ", 0, handle_repick_command},
		{"REMATCH", 1, handle_rematch_command},
		{"QUIT", 1, handle_quit_command},
	};

	for (size_t i = 0; i < sizeof(dispatch) / sizeof(dispatch[0]); i++)
	{
		if (command_matches(&dispatch[i], line))
		{
			dispatch[i].handler(s, idx, line);
			return;
		}
	}

	(void)queue_line(p, "ERROR bad_command");
}