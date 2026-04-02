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

int main(void)
{
#ifndef _WIN32
    signal(SIGPIPE, SIG_IGN);
#endif

    if (net_init() != 0)
    {
        fatal("WSAStartup");
    }

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

    ServerState state;
    init_server_state(&state);

    printf("Server listening on port %d\n", PORT);

    while (1)
    {
        /*
         * Server I/O loop order:
         * 1) build select() fd sets,
         * 2) accept new clients,
         * 3) read and parse client lines,
         * 4) flush pending outbound buffers,
         * 5) run timer-driven state transitions.
         */
        fd_set readfds, writefds;
        FD_ZERO(&readfds);
        FD_ZERO(&writefds);

        FD_SET(listen_fd, &readfds);
        int maxfd = (int)listen_fd;

        for (int i = 0; i < MAX_PLAYERS; i++)
        {
            if (state.players[i].connected)
            {
                FD_SET(state.players[i].fd, &readfds);

                if (player_has_pending_output(&state.players[i]))
                {
                    FD_SET(state.players[i].fd, &writefds);
                }

                if ((int)state.players[i].fd > maxfd)
                {
                    maxfd = (int)state.players[i].fd;
                }
            }
        }

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

        if (FD_ISSET(listen_fd, &readfds))
        {
            socket_t client_fd = accept(listen_fd, NULL, NULL);
            if (client_fd != INVALID_SOCKET)
            {
                if (net_set_nonblocking(client_fd) < 0)
                {
                    CLOSESOCKET(client_fd);
                }
                else
                {
                    int idx = add_player(&state, client_fd);
                    if (idx >= 0)
                    {
                        if (queue_line(&state.players[idx], "INFO connected_send_HELLO_name") < 0)
                        {
                            drop_player(&state, idx, 0);
                        }
                    }
                    else
                    {
                        (void)send_line(client_fd, "ERROR server_full");
                        CLOSESOCKET(client_fd);
                    }
                }
            }
        }

        for (int i = 0; i < MAX_PLAYERS; i++)
        {
            if (!state.players[i].connected)
                continue;

            if (FD_ISSET(state.players[i].fd, &readfds))
            {
                int rc = read_into_player_buffer(&state.players[i]);
                if (rc <= 0)
                {
                    drop_player(&state, i, 1);
                    reevaluate_state(&state);
                    continue;
                }

                char line[MAX_LINE];
                while (pop_line(&state.players[i], line, sizeof(line)))
                {
                    handle_command(&state, i, line);
                }
            }
        }

        for (int i = 0; i < MAX_PLAYERS; i++)
        {
            if (!state.players[i].connected)
                continue;

            if (FD_ISSET(state.players[i].fd, &writefds))
            {
                int rc = flush_player_output(&state.players[i]);
                if (rc < 0)
                {
                    drop_player(&state, i, 1);
                    reevaluate_state(&state);
                }
            }
        }

        close_lobby_if_needed(&state);
        expire_unready_setup_players(&state);

        if (state.phase == PHASE_ROUND_ACTIVE &&
            time(NULL) >= state.round_deadline)
        {
            resolve_round(&state);
        }
    }

    net_cleanup();
    return 0;
}