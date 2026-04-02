#include "common.h"
#include "game.h"
#include "protocol.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "server_commands.h"
#include "server_state.h"

#ifndef PORT
#define PORT 4242
#endif

static void fatal(const char *msg)
{
    perror(msg);
    exit(1);
}

static socket_t create_listen_socket(void)
{
    socket_t listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd == INVALID_SOCKET)
        fatal("socket");

#ifdef _WIN32
    const char yes = 1;
#else
    const int yes = 1;
#endif
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

static void accept_new_clients(socket_t listen_fd, fd_set *readfds, ServerState *state)
{
    if (!FD_ISSET(listen_fd, readfds))
    {
        return;
    }

    socket_t client_fd = accept(listen_fd, NULL, NULL);
    if (client_fd == INVALID_SOCKET)
    {
        return;
    }

    if (net_set_nonblocking(client_fd) < 0)
    {
        CLOSESOCKET(client_fd);
        return;
    }

    int idx = add_player(state, client_fd);
    if (idx >= 0)
    {
        if (queue_line(&state->players[idx], "INFO connected_send_HELLO_name") < 0)
        {
            drop_player(state, idx, 0);
        }
        return;
    }

    (void)send_line(client_fd, "ERROR server_full");
    CLOSESOCKET(client_fd);
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

static void process_timers(ServerState *state)
{
    close_lobby_if_needed(state);
    expire_unready_setup_players(state);

    if (state->phase == PHASE_ROUND_ACTIVE &&
        time(NULL) >= state->round_deadline)
    {
        resolve_round(state);
    }
}

int main(void)
{
#ifndef _WIN32
    signal(SIGPIPE, SIG_IGN);
#endif

    if (net_init() != 0)
    {
        fatal("WSAStartup");
    }

    socket_t listen_fd = create_listen_socket();

    ServerState state;
    init_server_state(&state);

    printf("Server listening on port %d\n", PORT);

    while (1)
    {
        fd_set readfds, writefds;
        int maxfd = build_select_sets(listen_fd, &state, &readfds, &writefds);

        struct timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;

        int ready = select(maxfd + 1, &readfds, &writefds, NULL, &tv);
        if (ready < 0)
        {
            if (NET_INTERRUPTED(NET_LAST_ERROR()))
                continue;
            fatal("select");
        }

        accept_new_clients(listen_fd, &readfds, &state);
        process_player_reads(&state, &readfds);
        process_player_writes(&state, &writefds);
        process_timers(&state);
    }

    net_cleanup();
    return 0;
}