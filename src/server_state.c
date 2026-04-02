#include "server_state.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "game.h"
#include "protocol.h"

typedef void (*PhaseHandler)(ServerState *s);

typedef struct
{
	Phase phase;
	PhaseHandler handler;
} PhaseDispatchEntry;

static long seconds_left(time_t deadline)
{
	long rem = (long)(deadline - time(NULL));
	return rem > 0 ? rem : 0;
}

static void queue_broadcast(ServerState *s, const char *fmt, ...)
{
	char line[MAX_LINE];
	int to_drop[MAX_PLAYERS];
	int drop_count = 0;

	va_list args;
	va_start(args, fmt);
	vsnprintf(line, sizeof(line), fmt, args);
	va_end(args);

	for (int i = 0; i < MAX_PLAYERS; i++)
	{
		if (!s->players[i].connected)
			continue;

		if (queue_line(&s->players[i], "%s", line) < 0)
		{
			to_drop[drop_count++] = i;
		}
	}

	for (int k = 0; k < drop_count; k++)
	{
		drop_player(s, to_drop[k], 0);
	}
}

static void broadcast_game_state(ServerState *s)
{
	for (int i = 0; i < MAX_PLAYERS; i++)
	{
		if (player_is_registered(&s->players[i]))
		{
			queue_game_state_for_player(s, &s->players[i]);
		}
	}
}

static void broadcast_positions(ServerState *s)
{
	for (int i = 0; i < MAX_PLAYERS; i++)
	{
		Player *p = &s->players[i];
		if (player_is_admitted(p))
		{
			queue_broadcast(s, "PLAYER %s %c %d %d %d %d",
							p->name,
							p->choice_chosen ? p->choice : '?',
							p->x,
							p->y,
							p->alive,
							p->waiting);
		}
	}
}

static void start_active_round(ServerState *s)
{
	start_round(s);
	queue_broadcast(s, "ROUND_START %d %d", s->round_no, ROUND_SECONDS);
	broadcast_positions(s);
}

void finish_repicks(ServerState *s)
{
	for (int i = 0; i < MAX_PLAYERS; i++)
	{
		Player *p = &s->players[i];
		if (player_is_alive(p))
		{
			p->choice = p->repick_choice;
			p->repick_submitted = 0;
			p->repick_choice = 0;
		}
	}

	queue_broadcast(s, "REPICK_DONE");
	broadcast_positions(s);
	start_active_round(s);
}

static void begin_repicks(ServerState *s)
{
	s->phase = PHASE_REPICK;

	for (int i = 0; i < MAX_PLAYERS; i++)
	{
		Player *p = &s->players[i];
		p->in_round = 0;
		p->repick_submitted = 0;
		p->repick_choice = 0;
	}

	queue_broadcast(s, "REPICK_START");
	broadcast_positions(s);
}

int all_alive_repicked(ServerState *s)
{
	int alive = 0;

	for (int i = 0; i < MAX_PLAYERS; i++)
	{
		Player *p = &s->players[i];
		if (player_is_alive(p))
		{
			alive++;
			if (!p->repick_submitted)
			{
				return 0;
			}
		}
	}

	return alive > 0;
}

void drop_player(ServerState *s, int idx, int announce)
{
	char name[MAX_NAME];
	int should_announce = 0;

	if (!s->players[idx].connected)
	{
		return;
	}

	if (announce && s->players[idx].registered)
	{
		snprintf(name, sizeof(name), "%s", s->players[idx].name);
		should_announce = 1;
	}

	CLOSESOCKET(s->players[idx].fd);
	memset(&s->players[idx], 0, sizeof(Player));
	s->players[idx].x = -1;
	s->players[idx].y = -1;

	if (should_announce)
	{
		char line[MAX_LINE];
		snprintf(line, sizeof(line), "LEFT %s", name);

		for (int i = 0; i < MAX_PLAYERS; i++)
		{
			if (s->players[i].connected)
			{
				(void)queue_line(&s->players[i], "%s", line);
			}
		}

		broadcast_game_state(s);
	}
}

void queue_game_state_for_player(ServerState *s, Player *dst)
{
	(void)queue_line(dst, "STATE_BEGIN");

	if (s->phase == PHASE_LOBBY_OPEN)
	{
		if (s->join_deadline == 0)
		{
			(void)queue_line(dst, "LOBBY_WAITING");
		}
		else
		{
			(void)queue_line(dst, "LOBBY_OPEN %ld", seconds_left(s->join_deadline));
		}
	}
	else if (s->phase == PHASE_SETUP)
	{
		(void)queue_line(dst, "LOBBY_CLOSED");
		(void)queue_line(dst, "SETUP_OPEN %d", (int)seconds_left(s->setup_deadline));
	}
	else if (s->phase == PHASE_ROUND_ACTIVE)
	{
		(void)queue_line(dst, "LOBBY_CLOSED");
		(void)queue_line(dst, "ROUND_START %d %d", s->round_no, (int)seconds_left(s->round_deadline));
	}
	else if (s->phase == PHASE_REPICK)
	{
		(void)queue_line(dst, "LOBBY_CLOSED");
		(void)queue_line(dst, "REPICK_START");
	}
	else if (s->phase == PHASE_GAME_OVER)
	{
		const char *winner = "nobody";
		for (int i = 0; i < MAX_PLAYERS; i++)
		{
			Player *p = &s->players[i];
			if (player_is_alive(p))
			{
				winner = p->name;
				break;
			}
		}
		(void)queue_line(dst, "GAME_OVER %s", winner);
	}

	for (int i = 0; i < MAX_PLAYERS; i++)
	{
		Player *p = &s->players[i];
		if (player_is_admitted(p))
		{
			(void)queue_line(dst, "PLAYER %s %c %d %d %d %d",
							 p->name,
							 p->choice_chosen ? p->choice : '?',
							 p->x,
							 p->y,
							 p->alive,
							 p->waiting);
		}
	}

	(void)queue_line(dst, "STATE_END");
}

void maybe_admit_player(ServerState *s, int idx)
{
	Player *p = &s->players[idx];

	if (!p->connected || !p->registered)
		return;
	if (p->admitted)
		return;
	if (!p->choice_chosen || !p->spawn_chosen)
		return;

	if (s->phase != PHASE_LOBBY_OPEN)
	{
		(void)queue_line(p, "ERROR lobby_closed");
		return;
	}

	if (s->join_deadline == 0)
	{
		s->join_deadline = time(NULL) + JOIN_WINDOW_SECONDS;
		queue_broadcast(s, "LOBBY_OPEN %d", JOIN_WINDOW_SECONDS);
	}
	else if (time(NULL) >= s->join_deadline)
	{
		(void)queue_line(p, "ERROR lobby_closed");
		return;
	}

	p->admitted = 1;
	p->waiting = 1;

	(void)queue_line(p, "JOINED_MATCH");
	queue_broadcast(s, "JOINED %s", p->name);
	broadcast_game_state(s);
}

void reset_match(ServerState *s)
{
	s->phase = PHASE_LOBBY_OPEN;
	s->join_deadline = 0;
	s->setup_deadline = 0;
	s->round_deadline = 0;
	s->round_no = 0;

	for (int i = 0; i < MAX_PLAYERS; i++)
	{
		Player *p = &s->players[i];
		if (!player_is_registered(p))
			continue;

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

		(void)queue_line(p, "MATCH_RESET");
		(void)queue_line(p, "SPECTATING");
		(void)queue_line(p, "CHOOSE_TYPE");
		(void)queue_line(p, "CHOOSE_SPAWN %d %d", GRID_W, GRID_H);
		(void)queue_line(p, "LOBBY_WAITING");
	}
}

static void end_game(ServerState *s)
{
	s->phase = PHASE_GAME_OVER;

	for (int i = 0; i < MAX_PLAYERS; i++)
	{
		Player *p = &s->players[i];
		if (player_is_alive(p))
		{
			queue_broadcast(s, "GAME_OVER %s", p->name);
			return;
		}
	}

	queue_broadcast(s, "GAME_OVER nobody");
}

static void reevaluate_lobby_open(ServerState *s)
{
	if (admitted_count(s) == 0)
	{
		s->join_deadline = 0;
	}
}

static void reevaluate_setup(ServerState *s)
{
	if (admitted_count(s) < 2)
	{
		end_game(s);
		return;
	}

	if (all_admitted_ready(s))
	{
		start_active_round(s);
	}
}

static void reevaluate_repick(ServerState *s)
{
	if (active_alive_count(s) <= 1)
	{
		end_game(s);
		return;
	}

	if (all_alive_repicked(s))
	{
		finish_repicks(s);
	}
}

static void reevaluate_round_active(ServerState *s)
{
	if (active_alive_count(s) <= 1)
	{
		end_game(s);
	}
}

void reevaluate_state(ServerState *s)
{
	static const PhaseDispatchEntry dispatch[] = {
		{PHASE_LOBBY_OPEN, reevaluate_lobby_open},
		{PHASE_SETUP, reevaluate_setup},
		{PHASE_REPICK, reevaluate_repick},
		{PHASE_ROUND_ACTIVE, reevaluate_round_active},
	};

	if (s->phase == PHASE_GAME_OVER)
	{
		return;
	}

	for (size_t i = 0; i < sizeof(dispatch) / sizeof(dispatch[0]); i++)
	{
		if (dispatch[i].phase == s->phase)
		{
			dispatch[i].handler(s);
			return;
		}
	}
}

void close_lobby_if_needed(ServerState *s)
{
	if (s->phase != PHASE_LOBBY_OPEN)
		return;
	if (s->join_deadline == 0)
		return;
	if (time(NULL) < s->join_deadline)
		return;

	s->phase = PHASE_SETUP;
	s->setup_deadline = time(NULL) + SETUP_SECONDS;

	queue_broadcast(s, "LOBBY_CLOSED");
	queue_broadcast(s, "SETUP_OPEN %d", SETUP_SECONDS);

	if (admitted_count(s) < 2)
	{
		end_game(s);
		return;
	}

	if (all_admitted_ready(s))
	{
		start_active_round(s);
	}
	else
	{
		queue_broadcast(s, "WAITING_FOR_OTHERS");
	}
}

void expire_unready_setup_players(ServerState *s)
{
	if (s->phase != PHASE_SETUP)
		return;
	if (s->setup_deadline == 0)
		return;
	if (time(NULL) < s->setup_deadline)
		return;

	for (int i = 0; i < MAX_PLAYERS; i++)
	{
		Player *p = &s->players[i];
		if (player_is_admitted(p))
		{
			if (!p->choice_chosen || !p->spawn_chosen)
			{
				drop_player(s, i, 1);
			}
		}
	}

	if (admitted_count(s) < 2)
	{
		end_game(s);
		return;
	}

	if (all_admitted_ready(s))
	{
		start_active_round(s);
	}
}

void resolve_round(ServerState *s)
{
	Pair pairs[MAX_PLAYERS / 2];
	int bye_index = -1;
	int pair_count = build_pairs(s, pairs, MAX_PLAYERS / 2, &bye_index);

	for (int k = 0; k < pair_count; k++)
	{
		Player *a = &s->players[pairs[k].a];
		Player *b = &s->players[pairs[k].b];

		int ax = a->x;
		int ay = a->y;
		int bx = b->x;
		int by = b->y;

		int r = rps_result(a->choice, b->choice);

		if (r == 1)
		{
			a->x = bx;
			a->y = by;

			b->alive = 0;
			b->in_round = 0;
			b->x = -1;
			b->y = -1;

			queue_broadcast(s, "PAIR %s %s %c %c WINNER %s MOVE %d %d",
							a->name, b->name, a->choice, b->choice,
							a->name, a->x, a->y);
			if (b->connected)
			{
				(void)queue_line(b, "ELIMINATED lost");
			}
		}
		else if (r == -1)
		{
			b->x = ax;
			b->y = ay;

			a->alive = 0;
			a->in_round = 0;
			a->x = -1;
			a->y = -1;

			queue_broadcast(s, "PAIR %s %s %c %c WINNER %s MOVE %d %d",
							a->name, b->name, a->choice, b->choice,
							b->name, b->x, b->y);
			if (a->connected)
			{
				(void)queue_line(a, "ELIMINATED lost");
			}
		}
		else
		{
			queue_broadcast(s, "PAIR %s %s %c %c TIE",
							a->name, b->name, a->choice, b->choice);
		}
	}

	if (bye_index != -1)
	{
		queue_broadcast(s, "BYE %s", s->players[bye_index].name);
	}

	for (int i = 0; i < MAX_PLAYERS; i++)
	{
		s->players[i].in_round = 0;
	}

	broadcast_positions(s);

	if (active_alive_count(s) <= 1)
	{
		end_game(s);
		return;
	}

	if (all_alive_same_choice(s))
	{
		begin_repicks(s);
		return;
	}

	start_active_round(s);
}