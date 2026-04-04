#ifndef CHASE_SIMULATION_H
#define CHASE_SIMULATION_H

#include "common.h"

/*
 * Server-side continuous chase simulation.
 *
 * IMPORTANT:
 * This module is responsible only for movement and captures.
 * It does NOT decide phase transitions such as GAME_OVER or REPICK.
 * The server state machine remains the single authority for that.
 */

/* Chase parameters (in game units). */
#define CHASE_SPEED 1.0f
#define CAPTURE_RADIUS 0.5f

/*
 * Simulate one chase tick.
 *
 * For each alive player:
 * - find nearest edible prey
 * - move toward that prey
 * - eliminate prey on contact
 *
 * Returns non-zero if any movement or elimination occurred, 0 otherwise.
 */
int simulate_chase_tick(ServerState *s, float dt_seconds);

/*
 * Return 1 if eater can eliminate prey under RPS rules, 0 otherwise.
 * R eats S, S eats P, P eats R.
 */
int can_eat(char eater, char prey);

/*
 * Find the nearest alive edible prey for the player at hunter_idx.
 * Returns the player-array index of the prey, or -1 if none exists.
 */
int find_nearest_prey(ServerState *s, int hunter_idx);

#endif