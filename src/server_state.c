#include "server_state.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "chase_simulation.h"
#include "game.h"
#include "protocol.h"

/*
 * State transition engine and server-side broadcasts.
 *
 * This module is responsible for phase changes, snapshots, round resolution,
 * and join/rematch lifecycle updates.
 */

static long seconds_left(time_t deadline)
{
	long rem = (long)(deadline - time(NULL));
	return rem > 0 ? rem : 0;
}

static int queue_line_checked(Player *p, const char *fmt, ...)
{
	char line[MAX_LINE];
	va_list args;

	va_start(args, fmt);
	int n = vsnprintf(line, sizeof(line), fmt, args);
	va_end(args);

	if (n < 0 || n >= (int)sizeof(line))
	{
		return -1;
	}

	return queue_line(p, "%s", line);
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

	if (drop_count > 0)
	{
		reevaluate_state(s);
	}
}

static void broadcast_game_state(ServerState *s)
{
	int dropped = 0;

	for (int i = 0; i < MAX_PLAYERS; i++)
	{
		if (player_is_registered(&s->players[i]))
		{
			if (queue_game_state_for_player(s, &s->players[i]) < 0)
			{
				drop_player(s, i, 0);
				dropped = 1;
			}
		}
	}

	if (dropped)
	{
		reevaluate_state(s);
	}
}

void broadcast_positions(ServerState *s)
{
	for (int i = 0; i < MAX_PLAYERS; i++)
	{
		Player *p = &s->players[i];
		if (player_is_admitted(p))
		{
			queue_broadcast(s, "PLAYER %s %c %.3f %.3f %d %d",
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
	char name[MAX_NAME_LENGTH];
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
	s->players[idx].x = -1.0f;
	s->players[idx].y = -1.0f;

	if (should_announce)
	{
		queue_broadcast(s, "LEFT %s", name);
		broadcast_game_state(s);
	}
}

static int player_won_match(const Player *p)
{
	return player_is_admitted(p) && (p->alive || p->waiting);
}

static int queue_game_over_result(Player *p)
{
	if (!p->connected || !p->registered)
	{
		return 0;
	}

	if (player_won_match(p))
	{
		return queue_line_checked(p, "GAME_OVER WIN");
	}
	if (player_is_admitted(p))
	{
		return queue_line_checked(p, "GAME_OVER LOSE");
	}

	return queue_line_checked(p, "GAME_OVER");
}

int queue_game_state_for_player(ServerState *s, Player *dst)
{
	if (queue_line_checked(dst, "STATE_BEGIN") < 0)
	{
		return -1;
	}

	if (s->phase == PHASE_LOBBY_OPEN)
	{
		if (s->join_deadline == 0)
		{
			if (queue_line_checked(dst, "LOBBY_WAITING") < 0)
			{
				return -1;
			}
		}
		else
		{
			if (queue_line_checked(dst, "LOBBY_OPEN %ld", seconds_left(s->join_deadline)) < 0)
			{
				return -1;
			}
		}
	}
	else if (s->phase == PHASE_SETUP)
	{
		if (queue_line_checked(dst, "LOBBY_CLOSED") < 0 ||
			queue_line_checked(dst, "SETUP_OPEN %d", (int)seconds_left(s->setup_deadline)) < 0)
		{
			return -1;
		}
	}
	else if (s->phase == PHASE_ROUND_ACTIVE)
	{
		if (queue_line_checked(dst, "LOBBY_CLOSED") < 0 ||
			queue_line_checked(dst, "ROUND_START %d %d", s->round_no, (int)seconds_left(s->round_deadline)) < 0)
		{
			return -1;
		}
	}
	else if (s->phase == PHASE_REPICK)
	{
		if (queue_line_checked(dst, "LOBBY_CLOSED") < 0 ||
			queue_line_checked(dst, "REPICK_START") < 0)
		{
			return -1;
		}
	}
	else if (s->phase == PHASE_GAME_OVER)
	{
		if (queue_game_over_result(dst) < 0)
		{
			return -1;
		}
	}

	for (int i = 0; i < MAX_PLAYERS; i++)
	{
		Player *p = &s->players[i];
		if (player_is_admitted(p))
		{
			if (queue_line_checked(dst, "PLAYER %s %c %.3f %.3f %d %d",
								   p->name,
								   p->choice_chosen ? p->choice : '?',
								   p->x,
								   p->y,
								   p->alive,
								   p->waiting) < 0)
			{
				return -1;
			}
		}
	}

	if (queue_line_checked(dst, "STATE_END") < 0)
	{
		return -1;
	}

	return 0;
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
		if (queue_line_checked(p, "ERROR lobby_closed") < 0)
		{
			drop_player(s, idx, 0);
			reevaluate_state(s);
		}
		return;
	}

	if (s->join_deadline == 0)
	{
		s->join_deadline = time(NULL) + JOIN_WINDOW_SECONDS;
		queue_broadcast(s, "LOBBY_OPEN %d", JOIN_WINDOW_SECONDS);
	}
	else if (time(NULL) >= s->join_deadline)
	{
		if (queue_line_checked(p, "ERROR lobby_closed") < 0)
		{
			drop_player(s, idx, 0);
			reevaluate_state(s);
		}
		return;
	}

	p->admitted = 1;
	p->waiting = 1;

	if (queue_line_checked(p, "JOINED_MATCH") < 0)
	{
		drop_player(s, idx, 0);
		reevaluate_state(s);
		return;
	}
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

		if (queue_line_checked(p, "MATCH_RESET") < 0 ||
			queue_line_checked(p, "SPECTATING") < 0 ||
			queue_line_checked(p, "CHOOSE_TYPE") < 0 ||
			queue_line_checked(p, "CHOOSE_SPAWN %d %d", GRID_W, GRID_H) < 0 ||
			queue_line_checked(p, "LOBBY_WAITING") < 0)
		{
			drop_player(s, i, 0);
		}
	}
}

static void end_game(ServerState *s)
{
	s->phase = PHASE_GAME_OVER;

	for (int i = 0; i < MAX_PLAYERS; i++)
	{
		Player *p = &s->players[i];
		if (!p->connected)
		{
			continue;
		}

		if (queue_game_over_result(p) < 0)
		{
			drop_player(s, i, 0);
		}
	}
}

static int count_alive_choice_types(ServerState *s)
{
	int has_r = 0;
	int has_p = 0;
	int has_s = 0;

	for (int i = 0; i < MAX_PLAYERS; i++)
	{
		Player *p = &s->players[i];
		if (!player_is_alive(p))
		{
			continue;
		}

		if (p->choice == 'R')
		{
			has_r = 1;
		}
		else if (p->choice == 'P')
		{
			has_p = 1;
		}
		else if (p->choice == 'S')
		{
			has_s = 1;
		}
	}

	return has_r + has_p + has_s;
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
	if (active_alive_count(s) <= 1 || count_alive_choice_types(s) <= 1)
	{
		end_game(s);
	}
}

void reevaluate_state(ServerState *s)
{
	if (s->phase == PHASE_GAME_OVER)
	{
		return;
	}

	switch (s->phase)
	{
		case PHASE_LOBBY_OPEN:
			reevaluate_lobby_open(s);
			break;
		case PHASE_SETUP:
			reevaluate_setup(s);
			break;
		case PHASE_REPICK:
			reevaluate_repick(s);
			break;
		case PHASE_ROUND_ACTIVE:
			reevaluate_round_active(s);
			break;
		case PHASE_GAME_OVER:
		default:
			break;
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

void advance_match_timers(ServerState *s, double now, double *last_chase_tick, double chase_tick_seconds)
{
	close_lobby_if_needed(s);
	expire_unready_setup_players(s);

	if (s->phase != PHASE_ROUND_ACTIVE)
	{
		return;
	}

	if (now - *last_chase_tick < chase_tick_seconds)
	{
		return;
	}

	*last_chase_tick = now;
	int match_ended = simulate_chase_tick(s, (float)chase_tick_seconds);
	broadcast_positions(s);

	if (match_ended)
	{
		reevaluate_state(s);
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

		float ax = a->x;
		float ay = a->y;
		float bx = b->x;
		float by = b->y;

		int r = rps_result(a->choice, b->choice);

		if (r == 1)
		{
			a->x = bx;
			a->y = by;

			b->alive = 0;
			b->in_round = 0;
			b->x = -1.0f;
			b->y = -1.0f;

			queue_broadcast(s, "PAIR %s %s %c %c WINNER %s MOVE %.3f %.3f",
							a->name, b->name, a->choice, b->choice,
							a->name, a->x, a->y);
			if (b->connected)
			{
				if (queue_line_checked(b, "ELIMINATED lost") < 0)
				{
					drop_player(s, pairs[k].b, 0);
				}
			}
		}
		else if (r == -1)
		{
			b->x = ax;
			b->y = ay;

			a->alive = 0;
			a->in_round = 0;
			a->x = -1.0f;
			a->y = -1.0f;

			queue_broadcast(s, "PAIR %s %s %c %c WINNER %s MOVE %.3f %.3f",
							a->name, b->name, a->choice, b->choice,
							b->name, b->x, b->y);
			if (a->connected)
			{
				if (queue_line_checked(a, "ELIMINATED lost") < 0)
				{
					drop_player(s, pairs[k].a, 0);
				}
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