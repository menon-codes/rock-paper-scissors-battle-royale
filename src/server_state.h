#ifndef SERVER_STATE_H
#define SERVER_STATE_H

#include "common.h"

/*
 * Server state transitions and server-originated broadcasts.
 * This module mutates match phase, admission, round resolution, and snapshots.
 */
void drop_player(ServerState *s, int idx, int announce);
void queue_game_state_for_player(ServerState *s, Player *dst);
void maybe_admit_player(ServerState *s, int idx);
void reset_match(ServerState *s);
void reevaluate_state(ServerState *s);
void close_lobby_if_needed(ServerState *s);
void expire_unready_setup_players(ServerState *s);
int all_alive_repicked(ServerState *s);
void finish_repicks(ServerState *s);
void resolve_round(ServerState *s);

#endif