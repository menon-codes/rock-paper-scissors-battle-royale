#include "game.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

/*
 * Core match computations:
 * - readiness checks
 * - round setup
 * - pairing and RPS resolution
 */

static int dist2(const Player *a, const Player *b)
{
    /* Squared distance avoids floating point and preserves ordering. */
    int dx = a->x - b->x;
    int dy = a->y - b->y;
    return dx * dx + dy * dy;
}

void init_server_state(ServerState *s)
{
    memset(s, 0, sizeof(*s));
    s->next_id = 1;
    s->phase = PHASE_LOBBY_OPEN;
    s->join_deadline = 0;
    s->round_deadline = 0;
    srand((unsigned int)time(NULL));
}

int admitted_count(ServerState *s)
{
    int count = 0;
    for (int i = 0; i < MAX_PLAYERS; i++)
    {
        Player *p = &s->players[i];
        if (player_is_admitted(p))
        {
            count++;
        }
    }
    return count;
}

int active_alive_count(ServerState *s)
{
    int count = 0;
    for (int i = 0; i < MAX_PLAYERS; i++)
    {
        Player *p = &s->players[i];
        if (player_is_alive(p))
        {
            count++;
        }
    }
    return count;
}

int all_admitted_ready(ServerState *s)
{
    int count = 0;

    for (int i = 0; i < MAX_PLAYERS; i++)
    {
        Player *p = &s->players[i];
        if (player_is_admitted(p))
        {
            count++;
            if (!p->choice_chosen || !p->spawn_chosen)
            {
                return 0;
            }
        }
    }

    return count >= 2;
}

int all_alive_same_choice(ServerState *s)
{
    char first = 0;
    int seen = 0;

    for (int i = 0; i < MAX_PLAYERS; i++)
    {
        Player *p = &s->players[i];
        if (player_is_alive(p))
        {
            if (!seen)
            {
                first = p->choice;
                seen = 1;
            }
            else if (p->choice != first)
            {
                return 0;
            }
        }
    }

    return seen && active_alive_count(s) >= 2;
}

void start_round(ServerState *s)
{
    s->phase = PHASE_ROUND_ACTIVE;
    s->round_no++;
    s->round_deadline = time(NULL) + ROUND_SECONDS;

    for (int i = 0; i < MAX_PLAYERS; i++)
    {
        Player *p = &s->players[i];
        p->in_round = 0;
        p->repick_submitted = 0;
        p->repick_choice = 0;

        if (!player_is_admitted(p))
        {
            continue;
        }

        if (p->alive || p->waiting)
        {
            if (p->waiting)
            {
                p->alive = 1;
                p->waiting = 0;
            }
            p->in_round = 1;
        }
    }
}

int rps_result(char a, char b)
{
    if (a == b)
        return 0;
    if ((a == 'R' && b == 'S') ||
        (a == 'S' && b == 'P') ||
        (a == 'P' && b == 'R'))
    {
        return 1;
    }
    return -1;
}

int build_pairs(ServerState *s, Pair pairs[], int max_pairs, int *bye_index)
{
    /* Collect eligible round participants first, then greedily pair nearest ones. */
    int unmatched[MAX_PLAYERS];
    int unmatched_count = 0;

    for (int i = 0; i < MAX_PLAYERS; i++)
    {
        Player *p = &s->players[i];
        if (player_is_alive(p) && p->in_round)
        {
            unmatched[unmatched_count++] = i;
        }
    }

    int pair_count = 0;
    *bye_index = -1;

    while (unmatched_count >= 2 && pair_count < max_pairs)
    {
        int best_i = -1;
        int best_j = -1;
        int best_d = 0;

        for (int i = 0; i < unmatched_count; i++)
        {
            for (int j = i + 1; j < unmatched_count; j++)
            {
                int pi = unmatched[i];
                int pj = unmatched[j];
                int d = dist2(&s->players[pi], &s->players[pj]);

                int id_i = s->players[pi].id;
                int id_j = s->players[pj].id;
                int best_id_i = (best_i >= 0) ? s->players[unmatched[best_i]].id : 0;
                int best_id_j = (best_j >= 0) ? s->players[unmatched[best_j]].id : 0;

                if (best_i == -1 ||
                    d < best_d ||
                    (d == best_d && (id_i < best_id_i ||
                                     (id_i == best_id_i && id_j < best_id_j))))
                {
                    best_i = i;
                    best_j = j;
                    best_d = d;
                }
            }
        }

        pairs[pair_count].a = unmatched[best_i];
        pairs[pair_count].b = unmatched[best_j];
        pair_count++;

        if (best_i > best_j)
        {
            int tmp = best_i;
            best_i = best_j;
            best_j = tmp;
        }

        for (int k = best_j; k < unmatched_count - 1; k++)
        {
            unmatched[k] = unmatched[k + 1];
        }
        unmatched_count--;

        for (int k = best_i; k < unmatched_count - 1; k++)
        {
            unmatched[k] = unmatched[k + 1];
        }
        unmatched_count--;
    }

    if (unmatched_count == 1)
    {
        *bye_index = unmatched[0];
    }

    return pair_count;
}