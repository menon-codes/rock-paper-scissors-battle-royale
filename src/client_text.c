#include "protocol.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef PORT
#define PORT 4242
#endif

static void fatal(const char *msg)
{
    perror(msg);
    exit(1);
}

static int prompt_choice(char *out_choice)
{
    char buf[64];

    while (1)
    {
        printf("Enter R, P, or S: ");
        fflush(stdout);

        if (!fgets(buf, sizeof(buf), stdin))
        {
            return 0;
        }

        char c = (char)toupper((unsigned char)buf[0]);
        if (c == 'R' || c == 'P' || c == 'S')
        {
            *out_choice = c;
            return 1;
        }

        printf("Invalid choice. Please enter R, P, or S.\n");
    }
}

static int prompt_int(const char *prompt, int *out_value)
{
    char buf[64];
    int value;

    while (1)
    {
        printf("%s", prompt);
        fflush(stdout);

        if (!fgets(buf, sizeof(buf), stdin))
        {
            return 0;
        }

        if (sscanf(buf, "%d", &value) == 1)
        {
            *out_value = value;
            return 1;
        }

        printf("Invalid number. Try again.\n");
    }
}

static int prompt_join_inputs(char *choice, int *spawn_x, int *spawn_y)
{
    if (!prompt_choice(choice))
    {
        return 0;
    }
    if (!prompt_int("Enter x-coordinate of spawn: ", spawn_x))
    {
        return 0;
    }
    if (!prompt_int("Enter y-coordinate of spawn: ", spawn_y))
    {
        return 0;
    }
    return 1;
}

static void send_join_attempt(socket_t fd)
{
    char choice;
    int spawn_x, spawn_y;

    if (!prompt_join_inputs(&choice, &spawn_x, &spawn_y))
    {
        printf("Join cancelled.\n");
        return;
    }

    send_line(fd, "CHOICE %c", choice);
    send_line(fd, "SPAWN %d %d", spawn_x, spawn_y);

    printf("Sent join attempt: choice=%c spawn=(%d,%d)\n", choice, spawn_x, spawn_y);
}

int main(int argc, char **argv)
{
    const char *host = "127.0.0.1";
    const char *name = "player";

    if (argc >= 2)
    {
        name = argv[1];
    }

    if (net_init() != 0)
        fatal("WSAStartup");

    socket_t fd = connect_to_server(host, PORT);
    if (fd == INVALID_SOCKET)
    {
        fatal("connect");
    }

    Player me;
    memset(&me, 0, sizeof(me));
    me.fd = fd;

    send_line(fd, "HELLO %s", name);

    printf("Connected as %s.\n", name);
    printf("Commands:\n");
    printf("  j = try to join current match\n");
    printf("  r/p/s = repick during REPICK phase\n");
    printf("  m = request rematch after GAME_OVER\n");
    printf("  q = quit\n");

    {
        char mode_buf[32];
        printf("Press j to try to join now, or s to spectate: ");
        fflush(stdout);

        if (fgets(mode_buf, sizeof(mode_buf), stdin))
        {
            char mode = (char)toupper((unsigned char)mode_buf[0]);
            if (mode == 'J')
            {
                send_join_attempt(fd);
            }
            else
            {
                printf("Spectating.\n");
            }
        }
    }

    while (1)
    {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(fd, &readfds);
        FD_SET(STDIN_FILENO, &readfds);

        int maxfd = (fd > STDIN_FILENO) ? fd : STDIN_FILENO;

        int ready = select(maxfd + 1, &readfds, NULL, NULL, NULL);
        if (ready < 0)
        {
            fatal("select");
        }

        if (FD_ISSET(fd, &readfds))
        {
            int rc = read_into_player_buffer(&me);
            if (rc <= 0)
            {
                printf("Disconnected from server.\n");
                break;
            }

            char line[MAX_LINE];
            while (pop_line(&me, line, sizeof(line)))
            {
                printf("[SERVER] %s\n", line);
            }
        }

        if (FD_ISSET(STDIN_FILENO, &readfds))
        {
            char buf[64];
            if (!fgets(buf, sizeof(buf), stdin))
            {
                break;
            }

            char c = (char)toupper((unsigned char)buf[0]);

            if (c == 'Q')
            {
                send_line(fd, "QUIT");
                break;
            }
            else if (c == 'J')
            {
                send_join_attempt(fd);
            }
            else if (c == 'R' || c == 'P' || c == 'S')
            {
                send_line(fd, "REPICK %c", c);
            }
            else if (c == 'M')
            {
                send_line(fd, "REMATCH");
            }
            else
            {
                printf("Use j to join, r/p/s for REPICK, m for rematch, or q to quit.\n");
            }
        }
    }

    CLOSESOCKET(fd);
    net_cleanup();
    return 0;
}