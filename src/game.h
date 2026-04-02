#ifndef GAME_H
#define GAME_H

#include "common.h"

/*
 * Core game-state helpers used by server-side orchestration modules.
 *
 * Functions here are pure game logic and lightweight predicates over
 * ServerState/Player fields.
 */

static inline int player_is_registered(const Player *p)
{
	return p->connected && p->registered;
}

static inline int player_is_admitted(const Player *p)
{
	return p->connected && p->registered && p->admitted;
}

static inline int player_is_alive(const Player *p)
{
	return p->connected && p->registered && p->admitted && p->alive;
}

/* Initialize a clean server state for a new process start. */
void init_server_state(ServerState *s);

/* Count players currently admitted to the active match. */
int admitted_count(ServerState *s);

/* Count players still alive in the active match. */
int active_alive_count(ServerState *s);

/*
 * Return non-zero when all admitted players have submitted CHOICE and SPAWN,
 * and at least 2 admitted players exist.
 */
int all_admitted_ready(ServerState *s);

/* Return non-zero when all alive players currently share the same choice. */
int all_alive_same_choice(ServerState *s);

/* Advance match into a new active round and reset round-scoped flags. */
void start_round(ServerState *s);

/*
 * Build nearest-neighbor pairings among alive players in-round.
 * Returns number of pairs written to pairs[]. bye_index is set to an unpaired
 * player index, or -1 if no bye is needed.
 */
int build_pairs(ServerState *s, Pair pairs[], int max_pairs, int *bye_index);

/* Return 1 when a beats b, -1 when b beats a, and 0 for a tie. */
int rps_result(char a, char b);

#endif