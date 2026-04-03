#ifndef CHASE_SIMULATION_H
#define CHASE_SIMULATION_H

#include "common.h"

/*
 * Server-side continuous chase simulation.
 *
 * Each tick, every alive player finds the nearest prey (any player of a type
 * they can eliminate), computes normalized movement toward it, and captures
 * on contact. Same-type players clip through one another.
 *
 * All coordinates are floats; movement is deterministic per tick.
 */

/* Chase parameters (in game units). */
#define CHASE_SPEED 0.8f	/* Units per second (scaled by tick dt). */
#define CAPTURE_RADIUS 0.5f /* Contact distance. */

/*
 * Simulate one tick of chase behavior:
 * - For each alive player, find nearest prey.
 * - Move player toward prey by CHASE_SPEED.
 * - Eliminate prey on capture.
 * - Return 1 if only one choice type remains (match end), 0 otherwise.
 */
int simulate_chase_tick(ServerState *s, float dt_seconds);

/*
 * Determine which type player eats: 'R' eats 'S', 'S' eats 'P', 'P' eats 'R'.
 * Return 1 if a can eat b, 0 otherwise.
 */
int can_eat(char eater, char prey);

/*
 * Find the closest prey of edible type within the entire player roster.
 * Ignore non-alive or same-player or same-type players.
 * Return player array index, or -1 if no edible prey found.
 */
int find_nearest_prey(ServerState *s, int hunter_idx);

#endif
