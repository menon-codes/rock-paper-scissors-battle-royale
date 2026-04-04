#include "server_commands.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "protocol.h"
#include "server_state.h"

/*
 * Command parser and dispatcher for one client line at a time.
 *
 * Each handler validates context (phase/player status), mutates player/server
 * state, and queues protocol responses.
 *
 * Supported commands:
 * - HELLO
 * - HELLO <name>
 * - CHOICE <R|P|S>
 * - SPAWN <x> <y>
 * - REPICK <R|P|S>
 * - CHANGE_NAME <new_name>
 * - GET_STATE
 * - REMATCH
 * - QUIT
 */

typedef void (*CommandHandler)(ServerState *s, int idx, const char *line);

typedef struct
{
    const char *token;
    int exact;
    CommandHandler handler;
} CommandDispatchEntry;

static long seconds_left(time_t deadline)
{
    long rem = (long)(deadline - time(NULL));
    return rem > 0 ? rem : 0;
}

static int parse_rps_choice(const char *line, int offset, char *out_choice)
{
    /* Accept uppercase/lowercase and normalize to uppercase. */
    char choice = line[offset];

    if (choice >= 'a' && choice <= 'z')
    {
        choice = (char)(choice - 'a' + 'A');
    }

    if (choice != 'R' && choice != 'P' && choice != 'S')
    {
        return 0;
    }

    *out_choice = choice;
    return 1;
}

static int require_registered_not_admitted(Player *p)
{
    if (!p->registered)
    {
        (void)queue_line(p, "ERROR register_first");
        return 0;
    }

    if (p->admitted)
    {
        (void)queue_line(p, "ERROR already_joined");
        return 0;
    }

    return 1;
}

static void queue_lobby_status_for_player(ServerState *s, Player *p)
{
    if (s->phase == PHASE_LOBBY_OPEN)
    {
        (void)queue_line(p, "CHOOSE_TYPE");
        (void)queue_line(p, "CHOOSE_SPAWN %d %d", GRID_W, GRID_H);

        if (s->join_deadline == 0)
        {
            (void)queue_line(p, "LOBBY_WAITING");
        }
        else
        {
            (void)queue_line(p, "LOBBY_OPEN %ld", seconds_left(s->join_deadline));
        }
        return;
    }

    (void)queue_line(p, "LOBBY_CLOSED");
}

static int find_name(ServerState *s, const char *name)
{
    for (int i = 0; i < MAX_PLAYERS; i++)
    {
        Player *p = &s->players[i];
        if (player_is_registered(p) && strcmp(p->name, name) == 0)
        {
            return i;
        }
    }
    return -1;
}

static int spawn_taken(ServerState *s, int x, int y, int ignore_idx)
{
    for (int i = 0; i < MAX_PLAYERS; i++)
    {
        if (i == ignore_idx)
            continue;

        Player *p = &s->players[i];
        if (!player_is_admitted(p))
            continue;
        if (!p->spawn_chosen)
            continue;

        if ((int)p->x == x && (int)p->y == y)
        {
            return 1;
        }
    }
    return 0;
}

static void reset_player_progress(Player *p)
{
    p->admitted = 0;
    p->alive = 0;
    p->waiting = 0;
    p->in_round = 0;

    p->choice_chosen = 0;
    p->spawn_chosen = 0;

    p->repick_submitted = 0;
    p->repick_choice = 0;

    p->choice = 0;
    p->x = -1.0f;
    p->y = -1.0f;
}

static void init_player_slot(Player *p, socket_t fd, int id)
{
    memset(p, 0, sizeof(*p));
    p->fd = fd;
    p->id = id;
    p->connected = 1;
    p->x = -1.0f;
    p->y = -1.0f;
}

static void queue_broadcast_from_commands(ServerState *s, const char *fmt, ...)
{
    /*
     * Local broadcast helper used by command handlers.
     * Queue to all connected clients, then drop any sockets whose output
     * buffers overflowed.
     */
    char line[MAX_LINE];
    int to_drop[MAX_PLAYERS];
    int drop_count = 0;

    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);

    if (n < 0 || n >= (int)sizeof(line))
    {
        return;
    }

    for (int i = 0; i < MAX_PLAYERS; i++)
    {
        if (!s->players[i].connected)
        {
            continue;
        }

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

int add_player(ServerState *s, socket_t fd)
{
    for (int i = 0; i < MAX_PLAYERS; i++)
    {
        if (!s->players[i].connected)
        {
            init_player_slot(&s->players[i], fd, s->next_id++);
            return i;
        }
    }
    return -1;
}

static void handle_hello_command(ServerState *s, int idx, const char *line)
{
    Player *p = &s->players[idx];
    char requested_name[MAX_NAME_LENGTH];
    int has_name = 0;

    if (p->registered)
    {
        (void)queue_line(p, "ERROR already_registered");
        return;
    }

    /*
     * Support both:
     *   HELLO
     *   HELLO <name>
     *
     * If no name is provided, auto-assign PLAYER<id>.
     */
    if (strcmp(line, "HELLO") == 0)
    {
        has_name = 0;
    }
    else if (sscanf(line + 6, "%31s", requested_name) == 1)
    {
        has_name = 1;
    }
    else
    {
        (void)queue_line(p, "ERROR usage_HELLO_name");
        return;
    }

    if (has_name)
    {
        if (find_name(s, requested_name) != -1)
        {
            (void)queue_line(p, "ERROR duplicate_name");
            return;
        }

        snprintf(p->name, sizeof(p->name), "%s", requested_name);
    }
    else
    {
        snprintf(p->name, sizeof(p->name), "PLAYER%d", p->id);
    }

    p->registered = 1;
    reset_player_progress(p);

    (void)queue_line(p, "WELCOME %d", p->id);
    (void)queue_line(p, "SPECTATING");
    queue_lobby_status_for_player(s, p);
}

static void handle_choice_command(ServerState *s, int idx, const char *line)
{
    Player *p = &s->players[idx];
    char choice;

    if (!require_registered_not_admitted(p))
    {
        return;
    }

    if (!parse_rps_choice(line, 7, &choice))
    {
        (void)queue_line(p, "ERROR bad_choice");
        return;
    }

    p->choice = choice;
    p->choice_chosen = 1;
    (void)queue_line(p, "CHOICE_OK %c", choice);

    maybe_admit_player(s, idx);
}

static void handle_spawn_command(ServerState *s, int idx, const char *line)
{
    /*
     * Spawns are grid positions, so parse them as integers.
     * This avoids accepting fractional coordinates such as 2.7 4.3.
     */
    Player *p = &s->players[idx];
    int x, y;

    if (!require_registered_not_admitted(p))
    {
        return;
    }

    if (sscanf(line + 6, "%d %d", &x, &y) != 2)
    {
        (void)queue_line(p, "ERROR usage_SPAWN_x_y");
        return;
    }

    if (x < 0 || x >= GRID_W || y < 0 || y >= GRID_H)
    {
        (void)queue_line(p, "ERROR bad_spawn");
        return;
    }

    if (spawn_taken(s, x, y, idx))
    {
        (void)queue_line(p, "ERROR spawn_taken");
        return;
    }

    p->x = (float)x;
    p->y = (float)y;
    p->spawn_chosen = 1;
    (void)queue_line(p, "SPAWN_OK %d %d", x, y);

    maybe_admit_player(s, idx);
}

static void handle_repick_command(ServerState *s, int idx, const char *line)
{
    Player *p = &s->players[idx];
    char choice;

    if (s->phase != PHASE_REPICK)
    {
        (void)queue_line(p, "ERROR not_in_repick_phase");
        return;
    }

    if (!p->registered || !p->admitted || !p->alive)
    {
        (void)queue_line(p, "ERROR not_alive");
        return;
    }

    if (p->repick_submitted)
    {
        (void)queue_line(p, "ERROR already_repicked");
        return;
    }

    if (!parse_rps_choice(line, 7, &choice))
    {
        (void)queue_line(p, "ERROR bad_choice");
        return;
    }

    p->repick_choice = choice;
    p->repick_submitted = 1;
    (void)queue_line(p, "REPICK_OK %c", choice);

    if (all_alive_repicked(s))
    {
        finish_repicks(s);
    }
    else
    {
        (void)queue_line(p, "REPICK_WAITING");
    }
}

static void handle_change_name_command(ServerState *s, int idx, const char *line)
{
    /*
     * Allow a registered player to change their name, provided the new name
     * is not already in use. Broadcast the rename so other clients can update
     * their local player lists.
     */
    Player *p = &s->players[idx];
    char old_name[MAX_NAME_LENGTH];
    char new_name[MAX_NAME_LENGTH];

    if (!p->registered)
    {
        (void)queue_line(p, "ERROR register_first");
        return;
    }

    if (sscanf(line + 12, "%31s", new_name) != 1)
    {
        (void)queue_line(p, "ERROR usage_CHANGE_NAME_name");
        return;
    }

    if (strcmp(new_name, p->name) == 0)
    {
        (void)queue_line(p, "NAME_OK %s", p->name);
        return;
    }

    if (find_name(s, new_name) != -1)
    {
        (void)queue_line(p, "ERROR duplicate_name");
        return;
    }

    snprintf(old_name, sizeof(old_name), "%s", p->name);
    snprintf(p->name, sizeof(p->name), "%s", new_name);

    (void)queue_line(p, "NAME_OK %s", p->name);
    queue_broadcast_from_commands(s, "RENAMED %s %s", old_name, p->name);
}

static void handle_get_state_command(ServerState *s, int idx, const char *line)
{
    (void)line;
    if (queue_game_state_for_player(s, &s->players[idx]) < 0)
    {
        drop_player(s, idx, 0);
        reevaluate_state(s);
    }
}

static void handle_rematch_command(ServerState *s, int idx, const char *line)
{
    (void)line;
    Player *p = &s->players[idx];

    if (s->phase != PHASE_GAME_OVER)
    {
        (void)queue_line(p, "ERROR rematch_not_available");
        return;
    }

    reset_match(s);
}

static void handle_quit_command(ServerState *s, int idx, const char *line)
{
    (void)line;
    drop_player(s, idx, 1);
    reevaluate_state(s);
}

static int command_matches(const CommandDispatchEntry *entry, const char *line)
{
    if (entry->exact)
    {
        return strcmp(line, entry->token) == 0;
    }
    return strncmp(line, entry->token, strlen(entry->token)) == 0;
}

void handle_command(ServerState *s, int idx, const char *line)
{
    Player *p = &s->players[idx];

    /* Prefix/keyword dispatch table for supported client commands. */
    static const CommandDispatchEntry dispatch[] = {
        {"HELLO", 1, handle_hello_command},
        {"HELLO ", 0, handle_hello_command},
        {"GET_STATE", 1, handle_get_state_command},
        {"CHOICE ", 0, handle_choice_command},
        {"SPAWN ", 0, handle_spawn_command},
        {"REPICK ", 0, handle_repick_command},
        {"CHANGE_NAME ", 0, handle_change_name_command},
        {"REMATCH", 1, handle_rematch_command},
        {"QUIT", 1, handle_quit_command},
    };

    for (size_t i = 0; i < sizeof(dispatch) / sizeof(dispatch[0]); i++)
    {
        if (command_matches(&dispatch[i], line))
        {
            dispatch[i].handler(s, idx, line);
            return;
        }
    }

    (void)queue_line(p, "ERROR bad_command");
}