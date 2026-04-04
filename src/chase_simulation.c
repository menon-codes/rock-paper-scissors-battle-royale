#include "chase_simulation.h"

#include <math.h>

#include "game.h"

/*
 * Continuous chase simulation: each alive player pursues the nearest edible prey.
 * Capture is immediate on contact.
 *
 * This file does NOT decide whether the match should end or whether REPICK should
 * begin. That decision belongs to the server state machine.
 */

int can_eat(char eater, char prey)
{
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

        if (!player_is_alive(candidate))
            continue;
        if (i == hunter_idx)
            continue;
        if (candidate->choice == hunter->choice)
            continue;
        if (!can_eat(hunter->choice, candidate->choice))
            continue;

        float dx = candidate->x - hunter->x;
        float dy = candidate->y - hunter->y;
        float dist = sqrtf(dx * dx + dy * dy);

        if (best_prey_idx == -1 || dist < best_dist)
        {
            best_prey_idx = i;
            best_dist = dist;
        }
    }

    return best_prey_idx;
}

int simulate_chase_tick(ServerState *s, float dt_seconds)
{
    int changed = 0;

    if (dt_seconds <= 0.0f)
    {
        return 0;
    }

    for (int i = 0; i < MAX_PLAYERS; i++)
    {
        Player *hunter = &s->players[i];

        if (!player_is_alive(hunter))
            continue;

        int prey_idx = find_nearest_prey(s, i);
        if (prey_idx == -1)
        {
            continue;
        }

        Player *prey = &s->players[prey_idx];

        /* Prey may have been eliminated earlier in this tick. */
        if (!player_is_alive(prey))
        {
            continue;
        }

        float dx = prey->x - hunter->x;
        float dy = prey->y - hunter->y;
        float dist = sqrtf(dx * dx + dy * dy);

        if (dist <= CAPTURE_RADIUS)
        {
            prey->alive = 0;
            prey->in_round = 0;
            prey->x = -1.0f;
            prey->y = -1.0f;
            changed = 1;
            continue;
        }

        float move_dist = CHASE_SPEED * dt_seconds;
        if (move_dist > dist)
        {
            move_dist = dist;
        }

        if (move_dist > 0.0f && dist > 0.0f)
        {
            float ux = dx / dist;
            float uy = dy / dist;

            hunter->x += ux * move_dist;
            hunter->y += uy * move_dist;
            changed = 1;
        }

        /* Recheck after movement. */
        if (player_is_alive(prey))
        {
            float ndx = prey->x - hunter->x;
            float ndy = prey->y - hunter->y;
            float ndist = sqrtf(ndx * ndx + ndy * ndy);

            if (ndist <= CAPTURE_RADIUS)
            {
                prey->alive = 0;
                prey->in_round = 0;
                prey->x = -1.0f;
                prey->y = -1.0f;
                changed = 1;
            }
        }
    }

    return changed;
}