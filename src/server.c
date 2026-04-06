#include "common.h"
#include "game.h"
#include "protocol.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "chase_simulation.h"
#include "server_commands.h"
#include "server_state.h"

/*
 * Server event loop:
 * - accepts new clients
 * - drains reads and queued writes
 * - advances timer-driven state transitions
 */

#ifndef PORT
#define PORT 4242
#endif

#define CHASE_TICK_SECONDS 0.016667
#define CHASE_TICK_USEC 16667

static void fatal(const char *msg)
{
    perror(msg);
    exit(1);
}

static double now_seconds(void)
{
    /* Use sub-second precision so chase ticks can run faster than 1 Hz. */
    struct timespec ts;
    (void)timespec_get(&ts, TIME_UTC);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static socket_t create_listen_socket(void)
{
    socket_t listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd == INVALID_SOCKET)
        fatal("socket");

    const int yes = 1;
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&yes, sizeof(yes)) < 0)
    {
        fatal("setsockopt");
    }

    if (net_set_nonblocking(listen_fd) < 0)
    {
        fatal("set_nonblocking");
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
        fatal("bind");
    if (listen(listen_fd, 16) < 0)
        fatal("listen");

    return listen_fd;
}

static int build_select_sets(socket_t listen_fd, ServerState *state, fd_set *readfds, fd_set *writefds)
{
    /* Track readable sockets for ingress and writable sockets with pending output. */
    FD_ZERO(readfds);
    FD_ZERO(writefds);

    FD_SET(listen_fd, readfds);
    int maxfd = (int)listen_fd;

    for (int i = 0; i < MAX_PLAYERS; i++)
    {
        if (!state->players[i].connected)
            continue;

        FD_SET(state->players[i].fd, readfds);

        if (player_has_pending_output(&state->players[i]))
        {
            FD_SET(state->players[i].fd, writefds);
        }

        if ((int)state->players[i].fd > maxfd)
        {
            maxfd = (int)state->players[i].fd;
        }
    }

    return maxfd;
}

static int accept_new_clients(socket_t listen_fd, fd_set *readfds, ServerState *state)
{
    if (!FD_ISSET(listen_fd, readfds))
    {
        return 0;
    }

    socket_t client_fd = accept(listen_fd, NULL, NULL);
    if (client_fd == INVALID_SOCKET)
    {
        return 0;
    }

    if (net_set_nonblocking(client_fd) < 0)
    {
        CLOSESOCKET(client_fd);
        return 0;
    }

    int idx = add_player(state, client_fd);
    if (idx >= 0)
    {
        if (queue_line(&state->players[idx], "INFO connected_send_HELLO_name") < 0)
        {
            drop_player(state, idx, 0);
        }
        return 1;
    }

    (void)send_line(client_fd, "ERROR server_full");
    CLOSESOCKET(client_fd);
    return 0;
}

static int connected_player_count(const ServerState *state)
{
    int count = 0;
    for (int i = 0; i < MAX_PLAYERS; i++)
    {
        if (state->players[i].connected)
        {
            count++;
        }
    }
    return count;
}

static void process_player_reads(ServerState *state, fd_set *readfds)
{
    for (int i = 0; i < MAX_PLAYERS; i++)
    {
        if (!state->players[i].connected)
            continue;

        if (!FD_ISSET(state->players[i].fd, readfds))
            continue;

        int rc = read_into_player_buffer(&state->players[i]);
        if (rc <= 0)
        {
            drop_player(state, i, 1);
            reevaluate_state(state);
            continue;
        }

        char line[MAX_LINE];
        while (pop_line(&state->players[i], line, sizeof(line)))
        {
            handle_command(state, i, line);
        }
    }
}

static void process_player_writes(ServerState *state, fd_set *writefds)
{
    for (int i = 0; i < MAX_PLAYERS; i++)
    {
        if (!state->players[i].connected)
            continue;

        if (!FD_ISSET(state->players[i].fd, writefds))
            continue;

        int rc = flush_player_output(&state->players[i]);
        if (rc < 0)
        {
            drop_player(state, i, 1);
            reevaluate_state(state);
        }
    }
}

static void process_timers(ServerState *state, double *last_chase_tick)
{
    close_lobby_if_needed(state);
    expire_unready_setup_players(state);

    /* Chase tick loop: ~60 Hz = every 16.667ms. */
    double now = now_seconds();
    double elapsed = now - *last_chase_tick;

    if (state->phase == PHASE_ROUND_ACTIVE && elapsed >= CHASE_TICK_SECONDS)
    {
        *last_chase_tick = now;
        int match_ended = simulate_chase_tick(state, (float)CHASE_TICK_SECONDS);

        /* Send updated positions after each chase tick. */
        broadcast_positions(state);

        if (match_ended)
        {
            /* One type remains: game over. Let reevaluate_state handle the transition. */
            reevaluate_state(state);
        }
    }
}

int main(void)
{
    signal(SIGPIPE, SIG_IGN);

    if (net_init() != 0)
    {
        fatal("WSAStartup");
    }

    socket_t listen_fd = create_listen_socket();
    const char *auto_exit_env = getenv("RPS_TEST_AUTO_EXIT");
    int auto_exit_for_tests = (auto_exit_env != NULL && auto_exit_env[0] != '\0' && auto_exit_env[0] != '0');
    int saw_client = 0;

    ServerState state;
    init_server_state(&state);
    double last_chase_tick = now_seconds();

    printf("Server listening on port %d\n", PORT);

    while (1)
    {
        fd_set readfds, writefds;
        int maxfd = build_select_sets(listen_fd, &state, &readfds, &writefds);

        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = CHASE_TICK_USEC; /* 50ms for ~20 Hz chase tick cadence. */

        int ready = select(maxfd + 1, &readfds, &writefds, NULL, &tv);
        if (ready < 0)
        {
            if (NET_INTERRUPTED(NET_LAST_ERROR()))
                continue;
            fatal("select");
        }

        if (accept_new_clients(listen_fd, &readfds, &state))
        {
            saw_client = 1;
        }
        process_player_reads(&state, &readfds);
        process_player_writes(&state, &writefds);
        process_timers(&state, &last_chase_tick);

        if (auto_exit_for_tests && saw_client && connected_player_count(&state) == 0)
        {
            break;
        }
    }

    CLOSESOCKET(listen_fd);
    net_cleanup();
    return 0;
}