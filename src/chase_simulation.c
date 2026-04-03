#include "chase_simulation.h"

#include <math.h>
#include <string.h>

#include "game.h"

/*
 * Continuous chase simulation: each alive player pursues the nearest edible prey.
 * Capture is immediate on contact. Same-type overlaps are allowed (clip through).
 * When only one choice type remains, the simulation ends and match is won.
 */

int can_eat(char eater, char prey)
{
	/* Rock eats Scissors, Scissors eats Paper, Paper eats Rock. */
	if (eater == 'R')
		return prey == 'S';
	if (eater == 'S')
		return prey == 'P';
	if (eater == 'P')
		return prey == 'R';
	return 0;
}

int find_nearest_prey(ServerState *s, int hunter_idx)
{
	Player *hunter = &s->players[hunter_idx];

	if (!player_is_alive(hunter))
	{
		return -1;
	}

	int best_prey_idx = -1;
	float best_dist = 0.0f;

	for (int i = 0; i < MAX_PLAYERS; i++)
	{
		Player *candidate = &s->players[i];

		/* Skip non-alive, self, or same-type players. */
		if (!player_is_alive(candidate))
			continue;
		if (i == hunter_idx)
			continue;
		if (candidate->choice == hunter->choice)
			continue;

		/* Check if hunter can eat this candidate. */
		if (!can_eat(hunter->choice, candidate->choice))
			continue;

		/* Calculate distance. */
		float dx = candidate->x - hunter->x;
		float dy = candidate->y - hunter->y;
		float dist = sqrtf(dx * dx + dy * dy);

		/* Update best if this is closer. */
		if (best_prey_idx == -1 || dist < best_dist)
		{
			best_prey_idx = i;
			best_dist = dist;
		}
	}

	return best_prey_idx;
}

static int count_choice_types_alive(ServerState *s)
{
	/* Count how many distinct choice types have at least one alive player. */
	int has_r = 0, has_s = 0, has_p = 0;

	for (int i = 0; i < MAX_PLAYERS; i++)
	{
		if (!player_is_alive(&s->players[i]))
			continue;

		if (s->players[i].choice == 'R')
			has_r = 1;
		else if (s->players[i].choice == 'S')
			has_s = 1;
		else if (s->players[i].choice == 'P')
			has_p = 1;
	}

	return has_r + has_s + has_p;
}

int simulate_chase_tick(ServerState *s, float dt_seconds)
{
	if (dt_seconds < 0.0f)
	{
		dt_seconds = 0.0f;
	}

	/* For each alive player, move toward nearest prey. */
	for (int i = 0; i < MAX_PLAYERS; i++)
	{
		Player *hunter = &s->players[i];

		if (!player_is_alive(hunter))
			continue;

		int prey_idx = find_nearest_prey(s, i);
		if (prey_idx == -1)
		{
			/* No prey available; hunter stops. */
			continue;
		}

		Player *prey = &s->players[prey_idx];

		/* Compute vector toward prey. */
		float dx = prey->x - hunter->x;
		float dy = prey->y - hunter->y;
		float dist = sqrtf(dx * dx + dy * dy);

		if (dist <= CAPTURE_RADIUS)
		{
			/* Capture: eliminate prey immediately. */
			prey->alive = 0;
			prey->x = -1.0f;
			prey->y = -1.0f;
		}
		else
		{
			/* Normalize and move by speed scaled to this tick duration. */
			float move_dist = CHASE_SPEED * dt_seconds;
			if (move_dist > dist)
			{
				move_dist = dist; /* Clamp to not overshoot. */
			}

			float ux = dx / dist;
			float uy = dy / dist;

			hunter->x += ux * move_dist;
			hunter->y += uy * move_dist;

			/* Recheck after movement so capture happens in the same tick on contact. */
			float ndx = prey->x - hunter->x;
			float ndy = prey->y - hunter->y;
			float ndist = sqrtf(ndx * ndx + ndy * ndy);
			if (ndist <= CAPTURE_RADIUS)
			{
				prey->alive = 0;
				prey->x = -1.0f;
				prey->y = -1.0f;
			}
		}
	}

	/* Check if match should end: only one choice type remains. */
	int types_alive = count_choice_types_alive(s);
	return types_alive <= 1;
}
