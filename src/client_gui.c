#include "client_gui_state.h"
#include "protocol.h"
#include "common.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "raylib.h"

/*
 * Interactive GUI client:
 * - drives frame-based input + rendering
 * - polls network each frame through pump_network
 * - maps UI actions to line protocol commands
 */

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
    int enabled;
    char choice;
    int spawn_x;
    int spawn_y;
    int hello_sent;
    int choice_sent;
    int spawn_sent;
} AutoJoinConfig;

static void fatal(const char *msg)
{
    perror(msg);
    exit(1);
}

static int is_valid_name_char(int c)
{
    /* Keep names protocol-safe and easy to parse server-side. */
    return isalnum(c) || c == '_';
}

static int parse_choice_arg(const char *value, char *out_choice)
{
    if (value == NULL || value[0] == '\0' || value[1] != '\0')
    {
        return 0;
    }

    char c = (char)toupper((unsigned char)value[0]);
    if (c != 'R' && c != 'P' && c != 'S')
    {
        return 0;
    }

    *out_choice = c;
    return 1;
}

static int parse_spawn_arg(const char *value, int max_exclusive, int *out)
{
    if (value == NULL || out == NULL)
    {
        return 0;
    }

    char *end = NULL;
    long parsed = strtol(value, &end, 10);
    if (end == value || *end != '\0')
    {
        return 0;
    }
    if (parsed < 0 || parsed >= max_exclusive)
    {
        return 0;
    }

    *out = (int)parsed;
    return 1;
}

static int resolve_asset_path(const char *file_name, char *resolved_path, size_t resolved_path_size)
{
    const char *asset_dirs[] = {
        "assets",
        "../assets",
        "../../assets"};

    for (int i = 0; i < (int)(sizeof(asset_dirs) / sizeof(asset_dirs[0])); ++i)
    {
        snprintf(resolved_path, resolved_path_size, "%s/%s", asset_dirs[i], file_name);
        if (FileExists(resolved_path))
        {
            return 1;
        }
    }

    return 0;
}

static Texture2D pick_player_texture(char choice, Texture2D rock, Texture2D paper, Texture2D scissor)
{
    if (choice == 'P')
    {
        return paper;
    }
    if (choice == 'S')
    {
        return scissor;
    }
    return rock;
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

static void draw_players(GuiPlayer players[], Texture2D rock, Texture2D paper, Texture2D scissor)
{
    const float player_sprite_size = 44.0f;

    for (int i = 0; i < MAX_PLAYERS; i++)
    {
        if (!players[i].used)
            continue;
        if (!(players[i].alive || players[i].waiting))
            continue;
        if (players[i].x < 0.0f || players[i].y < 0.0f)
            continue;

        /* Convert float world coordinates to screen coordinates. */
        float cx = (float)GRID_ORIGIN_X + players[i].x * (float)CELL_SIZE + (float)CELL_SIZE / 2.0f;
        float cy = (float)GRID_ORIGIN_Y + players[i].y * (float)CELL_SIZE + (float)CELL_SIZE / 2.0f;

        Texture2D texture = pick_player_texture(players[i].choice, rock, paper, scissor);
        Rectangle src = {0.0f, 0.0f, (float)texture.width, (float)texture.height};
        Rectangle dst = {
            cx - player_sprite_size / 2.0f,
            cy - player_sprite_size / 2.0f,
            player_sprite_size,
            player_sprite_size};
        DrawTexturePro(texture, src, dst, (Vector2){0.0f, 0.0f}, 0.0f, players[i].alive ? WHITE : LIGHTGRAY);

        char label[64];
        snprintf(label, sizeof(label), "%s(%c)", players[i].name, players[i].choice);
        DrawText(label, (int)cx - 28, (int)cy - 34, 14, BLACK);
    }
}

static void draw_legend_box(int x, int y, Texture2D rock, Texture2D paper, Texture2D scissor)
{
    const float legend_sprite_size = 20.0f;

    DrawRectangleRounded((Rectangle){(float)x, (float)y, 220.0f, 125.0f}, 0.15f, 8, LIGHTGRAY);
    DrawRectangleLines(x, y, 220, 125, GRAY);
    DrawText("Legend", x + 12, y + 10, 22, BLACK);

    DrawTexturePro(rock,
                   (Rectangle){0.0f, 0.0f, (float)rock.width, (float)rock.height},
                   (Rectangle){(float)x + 15.0f, (float)y + 35.0f, legend_sprite_size, legend_sprite_size},
                   (Vector2){0.0f, 0.0f},
                   0.0f,
                   WHITE);
    DrawText("Rock", x + 45, y + 36, 20, BLACK);

    DrawTexturePro(paper,
                   (Rectangle){0.0f, 0.0f, (float)paper.width, (float)paper.height},
                   (Rectangle){(float)x + 15.0f, (float)y + 65.0f, legend_sprite_size, legend_sprite_size},
                   (Vector2){0.0f, 0.0f},
                   0.0f,
                   WHITE);
    DrawText("Paper", x + 45, y + 66, 20, BLACK);

    DrawTexturePro(scissor,
                   (Rectangle){0.0f, 0.0f, (float)scissor.width, (float)scissor.height},
                   (Rectangle){(float)x + 15.0f, (float)y + 95.0f, legend_sprite_size, legend_sprite_size},
                   (Vector2){0.0f, 0.0f},
                   0.0f,
                   WHITE);
    DrawText("Scissors", x + 45, y + 96, 20, BLACK);
}

int main(int argc, char **argv)
{
    const char *host = "127.0.0.1";
    const char *initial_name = "?";
    AutoJoinConfig auto_join;
    memset(&auto_join, 0, sizeof(auto_join));

    if (argc >= 2)
    {
        initial_name = argv[1];
    }

    if (argc == 5)
    {
        auto_join.enabled = 1;
        if (!parse_choice_arg(argv[2], &auto_join.choice) ||
            !parse_spawn_arg(argv[3], GRID_W, &auto_join.spawn_x) ||
            !parse_spawn_arg(argv[4], GRID_H, &auto_join.spawn_y))
        {
            fprintf(stderr, "Usage: %s [name [R|P|S x y]]\n", argv[0]);
            return 1;
        }
    }
    else if (!(argc == 1 || argc == 2))
    {
        fprintf(stderr, "Usage: %s [name [R|P|S x y]]\n", argv[0]);
        return 1;
    }

    if (net_init() != 0)
        fatal("WSAStartup");

    socket_t fd = connect_to_server(host, PORT);
    if (fd == INVALID_SOCKET)
        fatal("connect");

    Player net_player;
    memset(&net_player, 0, sizeof(net_player));
    net_player.fd = fd;
    send_line(fd, "GET_STATE");

    GuiState state;
    init_gui_state(&state, initial_name);

    if (auto_join.enabled)
    {
        snprintf(state.status_text, sizeof(state.status_text),
                 "Auto mode: %s %c (%d,%d)", initial_name, auto_join.choice,
                 auto_join.spawn_x, auto_join.spawn_y);
    }

    SetConfigFlags(FLAG_MSAA_4X_HINT);
    InitWindow(WINDOW_W, WINDOW_H, "RPS Battle Royale");
    SetTargetFPS(60);

    char rock_path[512];
    char paper_path[512];
    char scissor_path[512];
    int has_rock = resolve_asset_path("rock.png", rock_path, sizeof(rock_path));
    int has_paper = resolve_asset_path("paper.png", paper_path, sizeof(paper_path));
    int has_scissor = resolve_asset_path("scissor.png", scissor_path, sizeof(scissor_path));

    if (!has_rock || !has_paper || !has_scissor)
    {
        fprintf(stderr, "Missing one or more required textures in assets/ (rock.png, paper.png, scissor.png)\n");
        CLOSESOCKET(fd);
        net_cleanup();
        CloseWindow();
        return 1;
    }

    Texture2D rock_texture = LoadTexture(rock_path);
    Texture2D paper_texture = LoadTexture(paper_path);
    Texture2D scissor_texture = LoadTexture(scissor_path);
    if (rock_texture.id == 0 || paper_texture.id == 0 || scissor_texture.id == 0)
    {
        fprintf(stderr, "Failed to load one or more textures from assets/\n");
        if (rock_texture.id != 0)
            UnloadTexture(rock_texture);
        if (paper_texture.id != 0)
            UnloadTexture(paper_texture);
        if (scissor_texture.id != 0)
            UnloadTexture(scissor_texture);
        CLOSESOCKET(fd);
        net_cleanup();
        CloseWindow();
        return 1;
    }

    SetTextureFilter(rock_texture, TEXTURE_FILTER_BILINEAR);
    SetTextureFilter(paper_texture, TEXTURE_FILTER_BILINEAR);
    SetTextureFilter(scissor_texture, TEXTURE_FILTER_BILINEAR);

    Rectangle rockBtn = {720, 430, 120, 42};
    Rectangle paperBtn = {850, 430, 120, 42};
    Rectangle scissorsBtn = {980, 430, 140, 42};
    Rectangle nameBox = {720, 495, 260, 42};
    Rectangle nameBtn = {990, 495, 130, 42};

    while (!WindowShouldClose())
    {
        /* Drain all currently available server messages before input/rendering. */
        pump_network(&net_player, &state);

        Vector2 mouse = GetMousePosition();

        if (auto_join.enabled)
        {
            if (!state.name_registered && !state.name_check_pending && !auto_join.hello_sent && state.name_input[0] != '\0')
            {
                snprintf(state.pending_name, sizeof(state.pending_name), "%s", state.name_input);
                send_line(fd, "HELLO %s", state.pending_name);
                state.name_check_pending = 1;
                state.state_request_sent = 0;
                auto_join.hello_sent = 1;
            }

            if (state.name_registered && !state.game_over && state.can_attempt_join && !state.joined_match && !state.repick_phase)
            {
                if (!auto_join.choice_sent)
                {
                    send_line(fd, "CHOICE %c", auto_join.choice);
                    auto_join.choice_sent = 1;
                }

                if (state.choice_confirmed && !auto_join.spawn_sent)
                {
                    send_line(fd, "SPAWN %d %d", auto_join.spawn_x, auto_join.spawn_y);
                    auto_join.spawn_sent = 1;
                }
            }
        }

        if (state.name_box_active && !state.name_registered)
        {
            int ch = GetCharPressed();
            while (ch > 0)
            {
                size_t len = strlen(state.name_input);
                if (len < sizeof(state.name_input) - 1 && is_valid_name_char(ch))
                {
                    state.name_input[len] = (char)ch;
                    state.name_input[len + 1] = '\0';
                }
                ch = GetCharPressed();
            }

            if (IsKeyPressed(KEY_BACKSPACE))
            {
                size_t len = strlen(state.name_input);
                if (len > 0)
                {
                    state.name_input[len - 1] = '\0';
                }
            }

            if (IsKeyPressed(KEY_ENTER) && !state.name_check_pending && state.name_input[0] != '\0')
            {
                snprintf(state.pending_name, sizeof(state.pending_name), "%s", state.name_input);
                send_line(fd, "HELLO %s", state.pending_name);
                state.name_check_pending = 1;
                state.state_request_sent = 0;
                snprintf(state.status_text, sizeof(state.status_text), "Checking name '%s'...", state.pending_name);
            }
        }

        if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON))
        {
            if (CheckCollisionPointRec(mouse, nameBox) && !state.name_registered)
            {
                state.name_box_active = 1;
            }
            else if (CheckCollisionPointRec(mouse, nameBtn) && !state.name_registered)
            {
                state.name_box_active = 0;
                if (!state.name_check_pending && state.name_input[0] != '\0')
                {
                    snprintf(state.pending_name, sizeof(state.pending_name), "%s", state.name_input);
                    send_line(fd, "HELLO %s", state.pending_name);
                    state.name_check_pending = 1;
                    state.state_request_sent = 0;
                    snprintf(state.status_text, sizeof(state.status_text), "Checking name '%s'...", state.pending_name);
                }
            }
            else
            {
                state.name_box_active = 0;
            }
        }

        if (!state.state_request_sent)
        {
            /* Request one fresh snapshot after significant local state transitions. */
            send_line(fd, "GET_STATE");
            state.state_request_sent = 1;
        }

        if (state.name_registered && !state.game_over && state.can_attempt_join && !state.joined_match && !state.repick_phase &&
            IsMouseButtonPressed(MOUSE_LEFT_BUTTON))
        {
            /* Join flow: choose CHOICE via button, then set SPAWN via grid click. */

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

        if (!state.game_over && state.repick_phase)
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

        if (state.game_over && IsKeyPressed(KEY_M))
        {
            send_line(fd, "REMATCH");
        }

        BeginDrawing();
        ClearBackground(RAYWHITE);

        /* World view (left) + control panel (right) are drawn every frame. */
        draw_grid();
        draw_players(state.players, rock_texture, paper_texture, scissor_texture);

        int panel_x = GRID_ORIGIN_X + GRID_W * CELL_SIZE + 40;
        int panel_y = GRID_ORIGIN_Y;

        DrawText("RPS Battle Royale", panel_x, panel_y, 28, BLACK);
        panel_y += 50;

        char me_buf[64];
        if (state.name_registered)
        {
            snprintf(me_buf, sizeof(me_buf), "You: %s", state.my_name);
        }
        else if (state.pending_name[0] != '\0')
        {
            snprintf(me_buf, sizeof(me_buf), "You: %s (checking)", state.pending_name);
        }
        else
        {
            snprintf(me_buf, sizeof(me_buf), "You: (not registered)");
        }
        DrawText(me_buf, panel_x, panel_y, 20, DARKBLUE);
        panel_y += 28;

        DrawText(state.status_text, panel_x, panel_y, 18, DARKGRAY);
        panel_y += 35;

        if (state.selected_choice == 'R' || state.selected_choice == 'P' || state.selected_choice == 'S')
        {
            char choice_buf[64];
            snprintf(choice_buf, sizeof(choice_buf), "Selected choice: %c", state.selected_choice);
            DrawText(choice_buf, panel_x, panel_y, 20, BLACK);
            panel_y += 28;
        }
        else
        {
            DrawText("Selected choice: none", panel_x, panel_y, 20, BLACK);
            panel_y += 28;
        }

        if (!state.game_over && state.lobby_end_time > GetTime())
        {
            int sec = (int)(state.lobby_end_time - GetTime() + 0.999);
            char buf[64];
            snprintf(buf, sizeof(buf), "Join window: %d", sec);
            DrawText(buf, panel_x, panel_y, 20, MAROON);
            panel_y += 28;
        }

        if (!state.game_over && state.setup_end_time > GetTime())
        {
            int sec = (int)(state.setup_end_time - GetTime() + 0.999);
            char buf[64];
            snprintf(buf, sizeof(buf), "Setup time left: %d", sec);
            DrawText(buf, panel_x, panel_y, 20, MAROON);
            panel_y += 28;
        }

        if (!state.game_over && state.round_end_time > GetTime() && !state.repick_phase)
        {
            int sec = (int)(state.round_end_time - GetTime() + 0.999);
            char buf[64];
            snprintf(buf, sizeof(buf), "Round ends in: %d", sec);
            DrawText(buf, panel_x, panel_y, 20, MAROON);
            panel_y += 28;
        }

        if (!state.joined_match && state.can_attempt_join)
        {
            DrawText("Click Rock/Paper/Scissors, then click a grid tile.", panel_x, panel_y, 18, BLACK);
            panel_y += 26;
        }

        if (state.repick_phase)
        {
            DrawText("REPICK PHASE", panel_x, panel_y, 24, RED);
            panel_y += 28;
            DrawText("Press R, P, or S", panel_x, panel_y, 20, BLACK);
            panel_y += 28;
        }

        if (state.game_over)
        {
            char end_buf[128];
            snprintf(end_buf, sizeof(end_buf), "Winner: %s", state.winner_name);
            DrawText("GAME OVER", panel_x, panel_y, 28, RED);
            panel_y += 35;
            DrawText(end_buf, panel_x, panel_y, 22, BLACK);
            panel_y += 28;
            DrawText("Press M for rematch", panel_x, panel_y, 20, DARKBLUE);
            panel_y += 28;
        }

        draw_legend_box(panel_x, panel_y, rock_texture, paper_texture, scissor_texture);
        panel_y += 145;

        DrawRectangleRec(rockBtn, state.selected_choice == 'R' ? PINK : LIGHTGRAY);
        DrawRectangleLinesEx(rockBtn, 2, BLACK);
        DrawText("Rock", (int)rockBtn.x + 28, (int)rockBtn.y + 10, 20, BLACK);

        DrawRectangleRec(paperBtn, state.selected_choice == 'P' ? SKYBLUE : LIGHTGRAY);
        DrawRectangleLinesEx(paperBtn, 2, BLACK);
        DrawText("Paper", (int)paperBtn.x + 24, (int)paperBtn.y + 10, 20, BLACK);

        DrawRectangleRec(scissorsBtn, state.selected_choice == 'S' ? LIME : LIGHTGRAY);
        DrawRectangleLinesEx(scissorsBtn, 2, BLACK);
        DrawText("Scissors", (int)scissorsBtn.x + 20, (int)scissorsBtn.y + 10, 20, BLACK);

        DrawText("Name:", (int)nameBox.x, (int)nameBox.y - 24, 20, DARKGRAY);
        DrawRectangleRec(nameBox, state.name_box_active ? WHITE : LIGHTGRAY);
        DrawRectangleLinesEx(nameBox, 2, state.name_box_active ? BLUE : DARKGRAY);
        DrawText(state.name_input[0] ? state.name_input : "type_name_here", (int)nameBox.x + 10, (int)nameBox.y + 10, 20, BLACK);

        DrawRectangleRec(nameBtn, (!state.name_registered && !state.name_check_pending) ? LIGHTGRAY : GRAY);
        DrawRectangleLinesEx(nameBtn, 2, BLACK);
        DrawText("Set Name", (int)nameBtn.x + 14, (int)nameBtn.y + 10, 20, BLACK);

        if (!state.name_registered)
        {
            DrawText("Set a unique name before joining.", panel_x, WINDOW_H - 145, 18, MAROON);
        }

        DrawText("Controls:", panel_x, WINDOW_H - 115, 20, DARKGRAY);
        DrawText("Click type button, then click grid to join", panel_x, WINDOW_H - 90, 18, GRAY);
        DrawText("R / P / S during REPICK", panel_x, WINDOW_H - 65, 18, GRAY);
        DrawText("M after GAME_OVER for rematch", panel_x, WINDOW_H - 40, 18, GRAY);

        EndDrawing();
    }

    send_line(fd, "QUIT");
    UnloadTexture(rock_texture);
    UnloadTexture(paper_texture);
    UnloadTexture(scissor_texture);
    CLOSESOCKET(fd);
    net_cleanup();
    CloseWindow();
    return 0;
}