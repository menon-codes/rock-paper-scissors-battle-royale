#ifndef SERVER_STATE_H
#define SERVER_STATE_H

#include "common.h"

/*
 * Server state transitions and server-originated broadcasts.
 * This module mutates match phase, admission, round resolution, and snapshots.
 */

/* Remove a connected player, optionally broadcasting LEFT <name>. */
void drop_player(ServerState *s, int idx, int announce);

/* Queue a full state snapshot for one player. Returns 0 or -1 on queue failure. */
int queue_game_state_for_player(ServerState *s, Player *dst);

/* Admit a player once all preconditions are met (name/choice/spawn/lobby-open). */
void maybe_admit_player(ServerState *s, int idx);

/* Reset to a fresh lobby while preserving registered connections. */
void reset_match(ServerState *s);

/* Re-evaluate phase transitions after player or state changes. */
void reevaluate_state(ServerState *s);

/* Close lobby when join timer expires and move to setup when appropriate. */
void close_lobby_if_needed(ServerState *s);

/* Drop admitted players who did not finish setup before setup deadline. */
void expire_unready_setup_players(ServerState *s);

/* Return non-zero when all alive players have submitted REPICK. */
int all_alive_repicked(ServerState *s);

/* Apply REPICK choices, notify clients, and start a new round. */
void finish_repicks(ServerState *s);

/* Resolve current round pairings, eliminations, movement, and end conditions. */
void resolve_round(ServerState *s);

#endif