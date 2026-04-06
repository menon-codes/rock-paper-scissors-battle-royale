#include "client_gui_state.h"
#include "protocol.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#if !RPS_WINDOWS_SOCKETS
#include <locale.h>
#if defined(__has_include)
#if __has_include(<ncursesw/ncurses.h>)
#include <ncursesw/ncurses.h>
#else
#include <ncurses.h>
#endif
#else
#include <ncurses.h>
#endif
#endif

/*
 * Full-screen terminal client that mirrors the GUI flow in a CLI environment.
 *
 * Controls:
 * - U: toggle name edit mode
 * - R/P/S: choose type (or REPICK during repick phase)
 * - Arrow keys: move spawn cursor
 * - Enter/Space: submit spawn
 * - M: rematch after game over
 * - G: refresh snapshot
 * - Q: quit
 */

#ifndef PORT
#define PORT 4242
#endif

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

#if !RPS_WINDOWS_SOCKETS
static int send_command_checked(socket_t fd, GuiState *state, const char *fmt, ...)
{
    char line[MAX_LINE];
    va_list args;

    va_start(args, fmt);
    int n = vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);

    if (n < 0 || n >= (int)sizeof(line))
    {
        snprintf(state->status_text, sizeof(state->status_text), "Outgoing command was too long.");
        return -1;
    }

    if (send_line(fd, "%s", line) < 0)
    {
        snprintf(state->status_text, sizeof(state->status_text), "Failed to send command to server.");
        return -1;
    }

    return 0;
}

static void fatal(const char *msg)
{
    perror(msg);
    exit(1);
}

static int is_valid_name_char(int c)
{
    return isalnum(c) || c == '_';
}
#endif

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

#if !RPS_WINDOWS_SOCKETS
static int clamp_int(int value, int min_value, int max_value)
{
    if (value < min_value)
        return min_value;
    if (value > max_value)
        return max_value;
    return value;
}

static int to_grid_coord(float value)
{
    if (value < 0.0f)
    {
        return -1;
    }
    return (int)(value + 0.5f);
}

static const char *choice_label(char choice)
{
    switch (choice)
    {
        case 'R': return "Rock";
        case 'P': return "Paper";
        case 'S': return "Scissors";
        default:  return "-";
    }
}

/* Hide all players' picks before the first ROUND_START arrives. */
static int choices_hidden(const GuiState *state)
{
    if (state->game_over)
    {
        return 0;
    }

    if (state->repick_phase)
    {
        return 0;
    }

    return state->round_end_time <= 0.0;
}

static char displayed_choice_char(const GuiState *state, char choice)
{
    if (choices_hidden(state))
    {
        return '?';
    }
    return choice ? choice : '?';
}

static const char *displayed_choice_label(const GuiState *state, char choice)
{
    if (choices_hidden(state))
    {
        return "Hidden";
    }
    return choice_label(choice);
}

static void maybe_send_hello(socket_t fd, GuiState *state)
{
    if (state->name_registered || state->name_check_pending || state->name_input[0] == '\0')
    {
        return;
    }

    snprintf(state->pending_name, sizeof(state->pending_name), "%s", state->name_input);
    if (send_command_checked(fd, state, "HELLO %s", state->pending_name) < 0)
    {
        return;
    }
    state->name_check_pending = 1;
    state->state_request_sent = 0;
    snprintf(state->status_text, sizeof(state->status_text), "Checking name '%s'...", state->pending_name);
}

static void draw_grid(const GuiState *state, int top, int left, int cursor_x, int cursor_y)
{
    mvprintw(top - 1, left, "Grid (%dx%d)", GRID_W, GRID_H);

    for (int y = 0; y < GRID_H; y++)
    {
        for (int x = 0; x < GRID_W; x++)
        {
            char marker = '.';

            for (int i = 0; i < MAX_PLAYERS; i++)
            {
                if (!state->players[i].used)
                    continue;
                if (!(state->players[i].alive || state->players[i].waiting))
                    continue;

                int px = to_grid_coord(state->players[i].x);
                int py = to_grid_coord(state->players[i].y);
                if (px == x && py == y)
                {
                    marker = displayed_choice_char(state, state->players[i].choice);
                    break;
                }
            }

            if (x == cursor_x && y == cursor_y)
            {
                mvprintw(top + y, left + x * 3, "[%c]", marker);
            }
            else
            {
                mvprintw(top + y, left + x * 3, " %c ", marker);
            }
        }
    }
}

static void draw_player_list(const GuiState *state, int top, int left, int max_rows)
{
    mvprintw(top, left, "Players");

    int row = top + 1;
    for (int i = 0; i < MAX_PLAYERS && row < top + max_rows; i++)
    {
        if (!state->players[i].used)
            continue;

        const char *life = state->players[i].alive ? "alive" : (state->players[i].waiting ? "waiting" : "out");
        mvprintw(row++, left, "%-10s %-9s (%4.1f,%4.1f) %-7s",
                 state->players[i].name,
                 displayed_choice_label(state, state->players[i].choice),
                 state->players[i].x,
                 state->players[i].y,
                 life);
    }
}

static void draw_status(const GuiState *state, int top, int left)
{
    double now = rps_now_seconds();

    mvprintw(top + 0, left, "Status: %s", state->status_text);
    mvprintw(top + 1, left, "You: %s",
             state->name_registered ? state->my_name : (state->pending_name[0] ? state->pending_name : "(not registered)"));

    /* Show your own selected choice locally even before round start. */
    mvprintw(top + 2, left, "Selected choice: %s", choice_label(state->selected_choice));

    if (!state->game_over && state->lobby_end_time > now)
    {
        int sec = (int)(state->lobby_end_time - now + 0.999);
        mvprintw(top + 3, left, "Join window: %d", sec);
    }
    if (!state->game_over && state->setup_end_time > now)
    {
        int sec = (int)(state->setup_end_time - now + 0.999);
        mvprintw(top + 4, left, "Setup time left: %d", sec);
    }
    if (!state->game_over && !state->repick_phase && state->round_end_time > now)
    {
        int sec = (int)(state->round_end_time - now + 0.999);
        mvprintw(top + 5, left, "Round timer: %d", sec);
    }

    if (choices_hidden(state))
    {
        mvprintw(top + 6, left, "Player picks are hidden until the round starts.");
    }

    if (state->repick_phase)
    {
        mvprintw(top + 7, left, "REPICK PHASE: press R/P/S");
    }
    if (state->game_over)
    {
        const char *result = "Game over";
        if (state->match_result > 0)
        {
            result = "You won";
        }
        else if (state->match_result < 0)
        {
            result = "You lost";
        }
        mvprintw(top + 8, left, "%s (press M for rematch)", result);
    }
}

static void draw_controls(int rows, int left, int name_edit_active)
{
    int base = rows - 7;
    if (base < 0)
    {
        base = 0;
    }

    mvprintw(base + 0, left, "Controls: U(toggle name edit) Enter(send name/spawn) Space(spawn) Arrows(move cursor)");
    mvprintw(base + 1, left, "R/P/S(select type or repick) M(rematch) G(refresh) Q(quit)");
    mvprintw(base + 2, left, "Name edit: %s", name_edit_active ? "ON" : "OFF");
}

static void render_ui(const GuiState *state, int cursor_x, int cursor_y, int name_edit_active)
{
    int rows, cols;
    getmaxyx(stdscr, rows, cols);

    erase();
    mvprintw(0, 2, "RPS Battle Royale - client_text (ncursesw)");
    mvprintw(1, 2, "Spawn cursor: (%d,%d)", cursor_x, cursor_y);
    mvprintw(2, 2, "Name input: %s%s", state->name_input, name_edit_active ? "_" : "");

    if (rows < 22 || cols < 108)
    {
        mvprintw(4, 2, "Terminal too small. Resize to at least 108x22.");
        refresh();
        return;
    }

    draw_grid(state, 5, 2, cursor_x, cursor_y);
    draw_player_list(state, 5, 38, rows - 13);
    draw_status(state, 5, 78);
    draw_controls(rows, 2, name_edit_active);
    refresh();
}
#endif

int main(int argc, char **argv)
{
    const char *host = "127.0.0.1";
    const char *initial_name = "player";
    AutoJoinConfig auto_join;
    memset(&auto_join, 0, sizeof(auto_join));

    if (argc == 5)
    {
        initial_name = argv[1];
        auto_join.enabled = 1;
        if (!parse_choice_arg(argv[2], &auto_join.choice) ||
            !parse_spawn_arg(argv[3], GRID_W, &auto_join.spawn_x) ||
            !parse_spawn_arg(argv[4], GRID_H, &auto_join.spawn_y))
        {
            fprintf(stderr, "Usage: %s [name] [R|P|S x y]\n", argv[0]);
            return 1;
        }
    }
    else if (argc == 4)
    {
        auto_join.enabled = 1;
        if (!parse_choice_arg(argv[1], &auto_join.choice) ||
            !parse_spawn_arg(argv[2], GRID_W, &auto_join.spawn_x) ||
            !parse_spawn_arg(argv[3], GRID_H, &auto_join.spawn_y))
        {
            fprintf(stderr, "Usage: %s [name] [R|P|S x y]\n", argv[0]);
            return 1;
        }
    }
    else if (argc == 2)
    {
        initial_name = argv[1];
    }
    else if (argc != 1)
    {
        fprintf(stderr, "Usage: %s [name] [R|P|S x y]\n", argv[0]);
        return 1;
    }

#if RPS_WINDOWS_SOCKETS
    (void)host;
    (void)initial_name;
    fprintf(stderr, "client_text ncursesw mode is not supported on Windows in this branch.\n");
    return 1;
#else
    if (net_init() != 0)
        fatal("WSAStartup");

    socket_t fd = connect_to_server(host, PORT);
    if (fd == INVALID_SOCKET)
        fatal("connect");

    Player net_player;
    memset(&net_player, 0, sizeof(net_player));
    net_player.fd = fd;

    GuiState state;
    init_gui_state(&state, initial_name);
    if (send_command_checked(fd, &state, "GET_STATE") == 0)
    {
        state.state_request_sent = 1;
    }

    setlocale(LC_ALL, "");
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    nodelay(stdscr, TRUE);
    curs_set(0);

    int running = 1;
    int cursor_x = 0;
    int cursor_y = 0;
    int name_edit_active = !auto_join.enabled;

    while (running)
    {
        pump_network(&net_player, &state);

        if (auto_join.enabled)
        {
            if (!state.name_registered && !state.name_check_pending && !auto_join.hello_sent && state.name_input[0] != '\0')
            {
                maybe_send_hello(fd, &state);
                auto_join.hello_sent = 1;
            }

            if (state.name_registered && !state.game_over && state.can_attempt_join && !state.joined_match && !state.repick_phase)
            {
                if (!auto_join.choice_sent)
                {
                    if (send_command_checked(fd, &state, "CHOICE %c", auto_join.choice) == 0)
                    {
                        state.state_request_sent = 0;
                        auto_join.choice_sent = 1;
                    }
                }

                if (state.choice_confirmed && !auto_join.spawn_sent)
                {
                    if (send_command_checked(fd, &state, "SPAWN %d %d", auto_join.spawn_x, auto_join.spawn_y) == 0)
                    {
                        state.state_request_sent = 0;
                        auto_join.spawn_sent = 1;
                    }
                }
            }
        }

        if (!state.state_request_sent)
        {
            if (send_command_checked(fd, &state, "GET_STATE") == 0)
            {
                state.state_request_sent = 1;
            }
        }

        int ch;
        while ((ch = getch()) != ERR)
        {
            if (ch == 'q' || ch == 'Q')
            {
                running = 0;
                break;
            }

            if (ch == 'g' || ch == 'G')
            {
                if (send_command_checked(fd, &state, "GET_STATE") == 0)
                {
                    state.state_request_sent = 1;
                }
                continue;
            }

            if (!state.name_registered && (ch == 'u' || ch == 'U'))
            {
                name_edit_active = !name_edit_active;
                continue;
            }

            if (!state.name_registered && name_edit_active)
            {
                if (ch == 27)
                {
                    name_edit_active = 0;
                    continue;
                }
                if (ch == KEY_BACKSPACE || ch == 127 || ch == 8)
                {
                    size_t len = strlen(state.name_input);
                    if (len > 0)
                    {
                        state.name_input[len - 1] = '\0';
                    }
                    continue;
                }
                if (ch == '\n' || ch == '\r' || ch == KEY_ENTER)
                {
                    maybe_send_hello(fd, &state);
                    continue;
                }
                if (is_valid_name_char(ch))
                {
                    size_t len = strlen(state.name_input);
                    if (len < sizeof(state.name_input) - 1)
                    {
                        state.name_input[len] = (char)ch;
                        state.name_input[len + 1] = '\0';
                    }
                    continue;
                }
            }

            if (ch == KEY_LEFT)
            {
                cursor_x = clamp_int(cursor_x - 1, 0, GRID_W - 1);
                continue;
            }
            if (ch == KEY_RIGHT)
            {
                cursor_x = clamp_int(cursor_x + 1, 0, GRID_W - 1);
                continue;
            }
            if (ch == KEY_UP)
            {
                cursor_y = clamp_int(cursor_y - 1, 0, GRID_H - 1);
                continue;
            }
            if (ch == KEY_DOWN)
            {
                cursor_y = clamp_int(cursor_y + 1, 0, GRID_H - 1);
                continue;
            }

            if ((ch == 'h' || ch == 'H') && !state.name_registered)
            {
                maybe_send_hello(fd, &state);
                continue;
            }

            if ((ch == 'm' || ch == 'M') && state.game_over)
            {
                if (send_command_checked(fd, &state, "REMATCH") == 0)
                {
                    state.state_request_sent = 0;
                }
                continue;
            }

            if (ch == 'r' || ch == 'R' || ch == 'p' || ch == 'P' || ch == 's' || ch == 'S')
            {
                char c = (char)toupper((unsigned char)ch);

                if (!state.game_over && state.repick_phase)
                {
                    if (send_command_checked(fd, &state, "REPICK %c", c) == 0)
                    {
                        state.state_request_sent = 0;
                    }
                    continue;
                }

                if (state.name_registered && !state.game_over && state.can_attempt_join && !state.joined_match && !state.repick_phase)
                {
                    if (send_command_checked(fd, &state, "CHOICE %c", c) == 0)
                    {
                        state.state_request_sent = 0;
                    }
                    continue;
                }
            }

            if ((ch == ' ' || ch == '\n' || ch == '\r' || ch == KEY_ENTER) &&
                state.name_registered && !state.game_over && state.can_attempt_join && !state.joined_match && !state.repick_phase)
            {
                if (state.choice_confirmed)
                {
                    if (send_command_checked(fd, &state, "SPAWN %d %d", cursor_x, cursor_y) == 0)
                    {
                        state.state_request_sent = 0;
                    }
                }
                continue;
            }
        }

        render_ui(&state, cursor_x, cursor_y, name_edit_active);
        napms(33);
    }

    (void)send_command_checked(fd, &state, "QUIT");
    endwin();
    CLOSESOCKET(fd);
    net_cleanup();
    return 0;
#endif
}