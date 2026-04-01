#include "protocol.h"
#include "common.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "raylib.h"

#ifndef PORT
#define PORT 4242
#endif

#define WINDOW_W 1180
#define WINDOW_H 780

#define GRID_ORIGIN_X 40
#define GRID_ORIGIN_Y 40
#define CELL_SIZE 60

typedef struct
{
    int used;
    char name[MAX_NAME];
    char choice;
    int x;
    int y;
    int alive;
    int waiting;
} GuiPlayer;

static void fatal(const char *msg)
{
    perror(msg);
    exit(1);
}

static socket_t connect_to_server(const char *host)
{
    socket_t fd;

    if (net_init() != 0)
        fatal("WSAStartup");

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == INVALID_SOCKET)
        fatal("socket");

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);

    if (inet_pton(AF_INET, host, &addr.sin_addr) <= 0)
    {
        fatal("inet_pton");
    }
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        fatal("connect");
    }

    return fd;
}

static int find_player(GuiPlayer players[], const char *name)
{
    for (int i = 0; i < MAX_PLAYERS; i++)
    {
        if (players[i].used && strcmp(players[i].name, name) == 0)
        {
            return i;
        }
    }
    return -1;
}

static int get_or_add_player(GuiPlayer players[], const char *name)
{
    int idx = find_player(players, name);
    if (idx != -1)
        return idx;

    for (int i = 0; i < MAX_PLAYERS; i++)
    {
        if (!players[i].used)
        {
            memset(&players[i], 0, sizeof(players[i]));
            players[i].used = 1;
            snprintf(players[i].name, sizeof(players[i].name), "%s", name);
            players[i].x = -1;
            players[i].y = -1;
            players[i].choice = '?';
            return i;
        }
    }
    return -1;
}

static Color choice_color(char c)
{
    if (c == 'R')
        return RED;
    if (c == 'P')
        return BLUE;
    if (c == 'S')
        return GREEN;
    return GRAY;
}

static void clear_gui_players(GuiPlayer players[])
{
    memset(players, 0, sizeof(GuiPlayer) * MAX_PLAYERS);
}

static void parse_server_line(
    const char *line,
    GuiPlayer players[],
    const char *my_name,
    char *status_text,
    size_t status_sz,
    int *repick_phase,
    int *game_over,
    int *can_attempt_join,
    int *joined_match,
    int *choice_confirmed,
    int *spawn_confirmed,
    char *selected_choice,
    double *lobby_end_time,
    double *setup_end_time,
    double *round_end_time,
    char *winner_name,
    size_t winner_sz)
{
    char name1[MAX_NAME], name2[MAX_NAME], winner[MAX_NAME];
    char choice;
    int x, y, alive, waiting;
    long sec_long;
    int round_no, seconds;

    if (strcmp(line, "MATCH_RESET") == 0)
    {
        clear_gui_players(players);
        *repick_phase = 0;
        *game_over = 0;
        *can_attempt_join = 1;
        *joined_match = 0;
        *choice_confirmed = 0;
        *spawn_confirmed = 0;
        *selected_choice = 0;
        *lobby_end_time = 0.0;
        *setup_end_time = 0.0;
        *round_end_time = 0.0;
        winner_name[0] = '\0';
        snprintf(status_text, status_sz, "Match reset. You are spectating again.");
        return;
    }

    if (strcmp(line, "SPECTATING") == 0)
    {
        snprintf(status_text, status_sz, "Spectating. Pick a type and click a tile if lobby is open.");
        return;
    }

    if (strcmp(line, "LOBBY_WAITING") == 0)
    {
        *can_attempt_join = 1;
        *lobby_end_time = 0.0;
        snprintf(status_text, status_sz, "Lobby idle. Be the first player to join.");
        return;
    }

    if (sscanf(line, "LOBBY_OPEN %ld", &sec_long) == 1)
    {
        *can_attempt_join = 1;
        *lobby_end_time = GetTime() + sec_long;
        snprintf(status_text, status_sz, "Lobby open. Choose type and click a spawn tile.");
        return;
    }

    if (strcmp(line, "LOBBY_CLOSED") == 0)
    {
        *can_attempt_join = 0;
        *lobby_end_time = 0.0;
        snprintf(status_text, status_sz, "Lobby closed. Spectating only.");
        return;
    }

    if (sscanf(line, "SETUP_OPEN %d", &seconds) == 1)
    {
        *setup_end_time = GetTime() + seconds;
        snprintf(status_text, status_sz, "Setup locked. Waiting for admitted players.");
        return;
    }

    if (strcmp(line, "CHOOSE_TYPE") == 0 || strncmp(line, "CHOOSE_SPAWN", 12) == 0)
    {
        *can_attempt_join = 1;
        return;
    }

    if (sscanf(line, "CHOICE_OK %c", &choice) == 1)
    {
        *selected_choice = choice;
        *choice_confirmed = 1;
        snprintf(status_text, status_sz, "Choice confirmed: %c", choice);
        return;
    }

    if (sscanf(line, "SPAWN_OK %d %d", &x, &y) == 2)
    {
        *spawn_confirmed = 1;
        snprintf(status_text, status_sz, "Spawn confirmed at (%d,%d)", x, y);
        return;
    }

    if (strcmp(line, "JOINED_MATCH") == 0)
    {
        *joined_match = 1;
        *can_attempt_join = 0;
        snprintf(status_text, status_sz, "You joined the current match.");
        return;
    }

    if (strncmp(line, "WAITING_FOR_OTHERS", 18) == 0)
    {
        snprintf(status_text, status_sz, "Waiting for other admitted players.");
        return;
    }

    if (strncmp(line, "ERROR spawn_taken", 17) == 0)
    {
        *spawn_confirmed = 0;
        snprintf(status_text, status_sz, "Spawn taken. Click another tile.");
        return;
    }

    if (strncmp(line, "ERROR lobby_closed", 18) == 0)
    {
        *can_attempt_join = 0;
        snprintf(status_text, status_sz, "Too late to join this match. Spectating only.");
        return;
    }

    if (strncmp(line, "ERROR already_joined", 20) == 0)
    {
        snprintf(status_text, status_sz, "Already joined this match.");
        return;
    }

    if (strncmp(line, "ERROR rematch_not_available", 27) == 0)
    {
        snprintf(status_text, status_sz, "Rematch only works after GAME_OVER.");
        return;
    }

    if (strncmp(line, "JOINED ", 7) == 0)
    {
        if (sscanf(line, "JOINED %31s", name1) == 1)
        {
            snprintf(status_text, status_sz, "%s joined the match", name1);
        }
        return;
    }

    if (sscanf(line, "LEFT %31s", name1) == 1)
    {
        int idx = find_player(players, name1);
        if (idx != -1)
        {
            players[idx].used = 0;
        }
        snprintf(status_text, status_sz, "%s left", name1);
        return;
    }

    if (sscanf(line, "PLAYER %31s %c %d %d %d %d",
               name1, &choice, &x, &y, &alive, &waiting) == 6)
    {
        int idx = get_or_add_player(players, name1);
        if (idx != -1)
        {
            players[idx].choice = choice;
            players[idx].x = x;
            players[idx].y = y;
            players[idx].alive = alive;
            players[idx].waiting = waiting;
        }

        if (strcmp(name1, my_name) == 0)
        {
            if (choice == 'R' || choice == 'P' || choice == 'S')
            {
                *selected_choice = choice;
            }
        }
        return;
    }

    if (sscanf(line, "ROUND_START %d %d", &round_no, &seconds) == 2)
    {
        *repick_phase = 0;
        *round_end_time = GetTime() + seconds;
        *setup_end_time = 0.0;
        snprintf(status_text, status_sz, "Round %d started", round_no);
        return;
    }

    if (strcmp(line, "REPICK_START") == 0)
    {
        *repick_phase = 1;
        snprintf(status_text, status_sz, "Repick phase. Press R, P, or S.");
        return;
    }

    if (strcmp(line, "REPICK_DONE") == 0)
    {
        *repick_phase = 0;
        snprintf(status_text, status_sz, "Repick finished");
        return;
    }

    if (strncmp(line, "PAIR ", 5) == 0)
    {
        char c1, c2;
        int move_x, move_y;

        if (sscanf(line, "PAIR %31s %31s %c %c WINNER %31s MOVE %d %d",
                   name1, name2, &c1, &c2, winner, &move_x, &move_y) == 7)
        {
            int idx_w = find_player(players, winner);
            const char *loser = (strcmp(winner, name1) == 0) ? name2 : name1;
            int idx_l = find_player(players, loser);

            if (idx_w != -1)
            {
                players[idx_w].x = move_x;
                players[idx_w].y = move_y;
                players[idx_w].alive = 1;
                players[idx_w].waiting = 0;
            }
            if (idx_l != -1)
            {
                players[idx_l].alive = 0;
                players[idx_l].waiting = 0;
                players[idx_l].x = -1;
                players[idx_l].y = -1;
            }

            snprintf(status_text, status_sz, "%s beat %s", winner, loser);
            return;
        }

        if (sscanf(line, "PAIR %31s %31s %c %c TIE", name1, name2, &c1, &c2) == 4)
        {
            snprintf(status_text, status_sz, "%s and %s tied", name1, name2);
            return;
        }
    }

    if (sscanf(line, "BYE %31s", name1) == 1)
    {
        snprintf(status_text, status_sz, "%s got a bye", name1);
        return;
    }

    if (sscanf(line, "GAME_OVER %31s", name1) == 1)
    {
        *game_over = 1;
        snprintf(winner_name, winner_sz, "%s", name1);
        snprintf(status_text, status_sz, "Game over. Press M for rematch.");
        return;
    }

    if (strncmp(line, "REPICK_OK", 9) == 0)
    {
        snprintf(status_text, status_sz, "Repick submitted");
        return;
    }

    if (strcmp(line, "REPICK_WAITING") == 0)
    {
        snprintf(status_text, status_sz, "Waiting for other players to repick");
        return;
    }

    if (strncmp(line, "ERROR ", 6) == 0)
    {
        snprintf(status_text, status_sz, "%s", line);
        return;
    }
}

static void pump_network(
    Player *net_player,
    GuiPlayer gui_players[],
    const char *my_name,
    char *status_text,
    size_t status_sz,
    int *repick_phase,
    int *game_over,
    int *can_attempt_join,
    int *joined_match,
    int *choice_confirmed,
    int *spawn_confirmed,
    char *selected_choice,
    double *lobby_end_time,
    double *setup_end_time,
    double *round_end_time,
    char *winner_name,
    size_t winner_sz)
{
    while (1)
    {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(net_player->fd, &readfds);

        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 0;

        int ready = select(net_player->fd + 1, &readfds, NULL, NULL, &tv);
        if (ready < 0)
        {
            if (NET_INTERRUPTED(NET_LAST_ERROR()))
                continue;
            break;
        }
        if (ready == 0)
        {
            break;
        }

        int rc = read_into_player_buffer(net_player);
        if (rc <= 0)
        {
            snprintf(status_text, status_sz, "Disconnected from server");
            *game_over = 1;
            break;
        }

        char line[MAX_LINE];
        while (pop_line(net_player, line, sizeof(line)))
        {
            parse_server_line(
                line,
                gui_players,
                my_name,
                status_text,
                status_sz,
                repick_phase,
                game_over,
                can_attempt_join,
                joined_match,
                choice_confirmed,
                spawn_confirmed,
                selected_choice,
                lobby_end_time,
                setup_end_time,
                round_end_time,
                winner_name,
                winner_sz);
        }
    }
}

static void draw_grid(void)
{
    for (int y = 0; y <= GRID_H; y++)
    {
        DrawLine(
            GRID_ORIGIN_X,
            GRID_ORIGIN_Y + y * CELL_SIZE,
            GRID_ORIGIN_X + GRID_W * CELL_SIZE,
            GRID_ORIGIN_Y + y * CELL_SIZE,
            LIGHTGRAY);
    }

    for (int x = 0; x <= GRID_W; x++)
    {
        DrawLine(
            GRID_ORIGIN_X + x * CELL_SIZE,
            GRID_ORIGIN_Y,
            GRID_ORIGIN_X + x * CELL_SIZE,
            GRID_ORIGIN_Y + GRID_H * CELL_SIZE,
            LIGHTGRAY);
    }
}

static void draw_players(GuiPlayer players[])
{
    for (int i = 0; i < MAX_PLAYERS; i++)
    {
        if (!players[i].used)
            continue;
        if (!(players[i].alive || players[i].waiting))
            continue;
        if (players[i].x < 0 || players[i].y < 0)
            continue;

        int cx = GRID_ORIGIN_X + players[i].x * CELL_SIZE + CELL_SIZE / 2;
        int cy = GRID_ORIGIN_Y + players[i].y * CELL_SIZE + CELL_SIZE / 2;

        Color col = players[i].alive ? choice_color(players[i].choice) : GRAY;
        DrawCircle(cx, cy, 18, col);
        DrawCircleLines(cx, cy, 18, BLACK);

        char label[64];
        snprintf(label, sizeof(label), "%s(%c)", players[i].name, players[i].choice);
        DrawText(label, cx - 28, cy - 34, 14, BLACK);
    }
}

static void draw_legend_box(int x, int y)
{
    DrawRectangleRounded((Rectangle){(float)x, (float)y, 220.0f, 125.0f}, 0.15f, 8, LIGHTGRAY);
    DrawRectangleLines(x, y, 220, 125, GRAY);
    DrawText("Legend", x + 12, y + 10, 22, BLACK);

    DrawCircle(x + 25, y + 45, 10, RED);
    DrawText("Rock", x + 45, y + 36, 20, BLACK);

    DrawCircle(x + 25, y + 75, 10, BLUE);
    DrawText("Paper", x + 45, y + 66, 20, BLACK);

    DrawCircle(x + 25, y + 105, 10, GREEN);
    DrawText("Scissors", x + 45, y + 96, 20, BLACK);
}

int main(int argc, char **argv)
{
    const char *host = "127.0.0.1";
    const char *name = "player";

    if (argc >= 2)
    {
        name = argv[1];
    }

    socket_t fd = connect_to_server(host);

    Player net_player;
    memset(&net_player, 0, sizeof(net_player));
    net_player.fd = fd;

    send_line(fd, "HELLO %s", name);

    GuiPlayer gui_players[MAX_PLAYERS];
    memset(gui_players, 0, sizeof(gui_players));

    char status_text[256] = "Connected";
    char winner_name[MAX_NAME] = "";
    int repick_phase = 0;
    int game_over = 0;

    int can_attempt_join = 0;
    int joined_match = 0;
    int choice_confirmed = 0;
    int spawn_confirmed = 0;
    char selected_choice = 0;

    double lobby_end_time = 0.0;
    double setup_end_time = 0.0;
    double round_end_time = 0.0;

    InitWindow(WINDOW_W, WINDOW_H, "RPS Battle Royale");
    SetTargetFPS(60);

    Rectangle rockBtn = {720, 250, 120, 42};
    Rectangle paperBtn = {850, 250, 120, 42};
    Rectangle scissorsBtn = {980, 250, 140, 42};

    while (!WindowShouldClose())
    {
        pump_network(
            &net_player,
            gui_players,
            name,
            status_text,
            sizeof(status_text),
            &repick_phase,
            &game_over,
            &can_attempt_join,
            &joined_match,
            &choice_confirmed,
            &spawn_confirmed,
            &selected_choice,
            &lobby_end_time,
            &setup_end_time,
            &round_end_time,
            winner_name,
            sizeof(winner_name));

        Vector2 mouse = GetMousePosition();

        if (!game_over && can_attempt_join && !joined_match && !repick_phase &&
            IsMouseButtonPressed(MOUSE_LEFT_BUTTON))
        {

            if (CheckCollisionPointRec(mouse, rockBtn))
            {
                send_line(fd, "CHOICE R");
            }
            else if (CheckCollisionPointRec(mouse, paperBtn))
            {
                send_line(fd, "CHOICE P");
            }
            else if (CheckCollisionPointRec(mouse, scissorsBtn))
            {
                send_line(fd, "CHOICE S");
            }
            else
            {
                int gx = (int)((mouse.x - GRID_ORIGIN_X) / CELL_SIZE);
                int gy = (int)((mouse.y - GRID_ORIGIN_Y) / CELL_SIZE);

                if (gx >= 0 && gx < GRID_W && gy >= 0 && gy < GRID_H)
                {
                    send_line(fd, "SPAWN %d %d", gx, gy);
                }
            }
        }

        if (!game_over && repick_phase)
        {
            if (IsKeyPressed(KEY_R))
            {
                send_line(fd, "REPICK R");
            }
            else if (IsKeyPressed(KEY_P))
            {
                send_line(fd, "REPICK P");
            }
            else if (IsKeyPressed(KEY_S))
            {
                send_line(fd, "REPICK S");
            }
        }

        if (game_over && IsKeyPressed(KEY_M))
        {
            send_line(fd, "REMATCH");
        }

        BeginDrawing();
        ClearBackground(RAYWHITE);

        draw_grid();
        draw_players(gui_players);

        int panel_x = GRID_ORIGIN_X + GRID_W * CELL_SIZE + 40;
        int panel_y = GRID_ORIGIN_Y;

        DrawText("RPS Battle Royale", panel_x, panel_y, 28, BLACK);
        panel_y += 50;

        char me_buf[64];
        snprintf(me_buf, sizeof(me_buf), "You: %s", name);
        DrawText(me_buf, panel_x, panel_y, 20, DARKBLUE);
        panel_y += 28;

        DrawText(status_text, panel_x, panel_y, 18, DARKGRAY);
        panel_y += 35;

        if (selected_choice == 'R' || selected_choice == 'P' || selected_choice == 'S')
        {
            char choice_buf[64];
            snprintf(choice_buf, sizeof(choice_buf), "Selected choice: %c", selected_choice);
            DrawText(choice_buf, panel_x, panel_y, 20, BLACK);
            panel_y += 28;
        }
        else
        {
            DrawText("Selected choice: none", panel_x, panel_y, 20, BLACK);
            panel_y += 28;
        }

        if (!game_over && lobby_end_time > GetTime())
        {
            int sec = (int)(lobby_end_time - GetTime() + 0.999);
            char buf[64];
            snprintf(buf, sizeof(buf), "Join window: %d", sec);
            DrawText(buf, panel_x, panel_y, 20, MAROON);
            panel_y += 28;
        }

        if (!game_over && setup_end_time > GetTime())
        {
            int sec = (int)(setup_end_time - GetTime() + 0.999);
            char buf[64];
            snprintf(buf, sizeof(buf), "Setup time left: %d", sec);
            DrawText(buf, panel_x, panel_y, 20, MAROON);
            panel_y += 28;
        }

        if (!game_over && round_end_time > GetTime() && !repick_phase)
        {
            int sec = (int)(round_end_time - GetTime() + 0.999);
            char buf[64];
            snprintf(buf, sizeof(buf), "Round ends in: %d", sec);
            DrawText(buf, panel_x, panel_y, 20, MAROON);
            panel_y += 28;
        }

        if (!joined_match && can_attempt_join)
        {
            DrawText("Click Rock/Paper/Scissors, then click a grid tile.", panel_x, panel_y, 18, BLACK);
            panel_y += 26;
        }

        if (repick_phase)
        {
            DrawText("REPICK PHASE", panel_x, panel_y, 24, RED);
            panel_y += 28;
            DrawText("Press R, P, or S", panel_x, panel_y, 20, BLACK);
            panel_y += 28;
        }

        if (game_over)
        {
            char end_buf[128];
            snprintf(end_buf, sizeof(end_buf), "Winner: %s", winner_name);
            DrawText("GAME OVER", panel_x, panel_y, 28, RED);
            panel_y += 35;
            DrawText(end_buf, panel_x, panel_y, 22, BLACK);
            panel_y += 28;
            DrawText("Press M for rematch", panel_x, panel_y, 20, DARKBLUE);
            panel_y += 28;
        }

        draw_legend_box(panel_x, panel_y);
        panel_y += 145;

        DrawRectangleRec(rockBtn, selected_choice == 'R' ? PINK : LIGHTGRAY);
        DrawRectangleLinesEx(rockBtn, 2, BLACK);
        DrawText("Rock", (int)rockBtn.x + 28, (int)rockBtn.y + 10, 20, BLACK);

        DrawRectangleRec(paperBtn, selected_choice == 'P' ? SKYBLUE : LIGHTGRAY);
        DrawRectangleLinesEx(paperBtn, 2, BLACK);
        DrawText("Paper", (int)paperBtn.x + 24, (int)paperBtn.y + 10, 20, BLACK);

        DrawRectangleRec(scissorsBtn, selected_choice == 'S' ? LIME : LIGHTGRAY);
        DrawRectangleLinesEx(scissorsBtn, 2, BLACK);
        DrawText("Scissors", (int)scissorsBtn.x + 20, (int)scissorsBtn.y + 10, 20, BLACK);

        DrawText("Controls:", panel_x, WINDOW_H - 115, 20, DARKGRAY);
        DrawText("Click type button, then click grid to join", panel_x, WINDOW_H - 90, 18, GRAY);
        DrawText("R / P / S during REPICK", panel_x, WINDOW_H - 65, 18, GRAY);
        DrawText("M after GAME_OVER for rematch", panel_x, WINDOW_H - 40, 18, GRAY);

        EndDrawing();
    }

    send_line(fd, "QUIT");
    CLOSESOCKET(fd);
    net_cleanup();
    CloseWindow();
    return 0;
}