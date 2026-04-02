#ifndef GAME_H
#define GAME_H

#include "common.h"

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

void init_server_state(ServerState *s);

int admitted_count(ServerState *s);
int active_alive_count(ServerState *s);
int all_admitted_ready(ServerState *s);
int all_alive_same_choice(ServerState *s);

void start_round(ServerState *s);

int build_pairs(ServerState *s, Pair pairs[], int max_pairs, int *bye_index);
int rps_result(char a, char b);

#endif