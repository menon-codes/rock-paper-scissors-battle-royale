#include "protocol.h"

#include <ctype.h>
#include <limits.h>
#include <ncurses.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * ncurses terminal client for live grid visualization.
 *
 * Keeps the existing line-based protocol unchanged:
 * - reads server messages through protocol.c helpers
 * - tracks PLAYER state locally
 * - redraws the board/status panel with ncurses
 * - sends the same commands as the text client
 *
 * UI features:
 * - live grid display
 * - status / event sidebar
 * - adaptive layout for narrow terminals
 * - rename support through CHANGE_NAME
 * - hides picks before the round begins
 * - server auto-assigns names as PLAYER<id>
 */

#ifndef PORT
#define PORT 4242
#endif

#define UI_TICK_USEC 100000
#define MIN_ROWS_STACKED 18
#define MIN_COLS_STACKED 40
#define SIDEBAR_WIDTH 38

typedef struct
{
    int present;
    char name[MAX_NAME_LENGTH];
    char choice;
    float x;
    float y;
    int alive;
    int waiting;
} ViewPlayer;

typedef struct
{
    socket_t fd;
    Player net_player;

    ViewPlayer live_players[MAX_PLAYERS];
    ViewPlayer snapshot_players[MAX_PLAYERS];
    int in_snapshot;

    int self_id;
    char self_name[MAX_NAME_LENGTH];

    Phase phase;

    char phase_line[128];
    char banner_line[128];
} UiState;

static int curses_started = 0;

static void fatal(const char *msg)
{
    if (curses_started)
    {
        endwin();
    }
    perror(msg);
    exit(1);
}

static void set_banner(UiState *ui, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vsnprintf(ui->banner_line, sizeof(ui->banner_line), fmt, args);
    va_end(args);
}

static void clear_view_players(ViewPlayer players[])
{
    memset(players, 0, sizeof(ViewPlayer) * MAX_PLAYERS);
}

static ViewPlayer *active_target_players(UiState *ui)
{
    return ui->in_snapshot ? ui->snapshot_players : ui->live_players;
}

static int find_view_player(ViewPlayer players[], const char *name)
{
    for (int i = 0; i < MAX_PLAYERS; i++)
    {
        if (players[i].present && strcmp(players[i].name, name) == 0)
        {
            return i;
        }
    }
    return -1;
}

static void remove_view_player(ViewPlayer players[], const char *name)
{
    int idx = find_view_player(players, name);
    if (idx >= 0)
    {
        memset(&players[idx], 0, sizeof(players[idx]));
    }
}

static void rename_view_player(ViewPlayer players[], const char *old_name, const char *new_name)
{
    int idx = find_view_player(players, old_name);
    if (idx >= 0)
    {
        snprintf(players[idx].name, sizeof(players[idx].name), "%s", new_name);
    }
}

static void upsert_view_player(ViewPlayer players[],
                               const char *name,
                               char choice,
                               float x,
                               float y,
                               int alive,
                               int waiting)
{
    int idx = find_view_player(players, name);

    if (idx < 0)
    {
        for (int i = 0; i < MAX_PLAYERS; i++)
        {
            if (!players[i].present)
            {
                idx = i;
                break;
            }
        }
    }

    if (idx < 0)
    {
        return;
    }

    players[idx].present = 1;
    snprintf(players[idx].name, sizeof(players[idx].name), "%s", name);
    players[idx].choice = choice;
    players[idx].x = x;
    players[idx].y = y;
    players[idx].alive = alive;
    players[idx].waiting = waiting;
}

static int count_live_players(const UiState *ui)
{
    int count = 0;

    for (int i = 0; i < MAX_PLAYERS; i++)
    {
        if (ui->live_players[i].present)
        {
            count++;
        }
    }

    return count;
}

static int choice_color_pair(char choice)
{
    switch (choice)
    {
    case 'R':
    case 'r':
        return 1;
    case 'P':
    case 'p':
        return 2;
    case 'S':
    case 's':
        return 3;
    default:
        return 0;
    }
}

static int should_hide_choices(const UiState *ui)
{
    return ui->phase == PHASE_LOBBY_OPEN || ui->phase == PHASE_SETUP;
}

static char sidebar_choice_symbol(const UiState *ui, const ViewPlayer *p)
{
    if (should_hide_choices(ui))
    {
        return '?';
    }

    return p->choice ? p->choice : '?';
}

static char board_cell_symbol(const UiState *ui, int x, int y)
{
    for (int i = 0; i < MAX_PLAYERS; i++)
    {
        const ViewPlayer *p = &ui->live_players[i];
        if (!p->present)
            continue;

        if ((int)p->x == x && (int)p->y == y)
        {
            if (should_hide_choices(ui))
            {
                return p->waiting ? 'o' : '@';
            }

            if (p->waiting)
            {
                return (char)tolower((unsigned char)p->choice);
            }
            return p->choice ? p->choice : '@';
        }
    }

    return '.';
}

static int board_render_width(void)
{
    return 4 + GRID_W * 2;
}

static int board_render_height(void)
{
    return 3 + GRID_H + 2;
}

static void draw_board(const UiState *ui, int top, int left)
{
    mvprintw(top, left, "Grid (%dx%d)", GRID_W, GRID_H);

    mvprintw(top + 1, left, "    ");
    for (int x = 0; x < GRID_W; x++)
    {
        printw("%2d", x);
    }

    for (int y = 0; y < GRID_H; y++)
    {
        mvprintw(top + 2 + y, left, "%2d |", y);

        for (int x = 0; x < GRID_W; x++)
        {
            char ch = board_cell_symbol(ui, x, y);
            int pair = choice_color_pair(ch);

            if (has_colors() && pair != 0)
            {
                attron(COLOR_PAIR(pair));
            }

            mvaddch(top + 2 + y, left + 4 + x * 2, ch);

            if (has_colors() && pair != 0)
            {
                attroff(COLOR_PAIR(pair));
            }
        }
    }

    mvprintw(top + 2 + GRID_H + 1, left,
             "Legend: \n R = Rock \n P = Paper \n S = Scissors \n o/@ hidden before round");
}

static const char *player_status_text(const ViewPlayer *p)
{
    if (p->alive)
        return "alive";
    if (p->waiting)
        return "wait";
    return "out";
}

static void draw_sidebar(const UiState *ui, int top, int left, int height)
{
    mvprintw(top, left, "Status");
    mvprintw(top + 1, left, "Name: %s", ui->self_name[0] ? ui->self_name : "(unknown)");
    mvprintw(top + 2, left, "Id: %d", ui->self_id);
    mvprintw(top + 3, left, "%s", ui->phase_line[0] ? ui->phase_line : "No phase info yet");
    mvprintw(top + 4, left, "Players shown: %d", count_live_players(ui));

    mvprintw(top + 6, left, "Last event:");
    mvprintw(top + 7, left, "%.*s", SIDEBAR_WIDTH - 2,
             ui->banner_line[0] ? ui->banner_line : "(none)");

    mvprintw(top + 9, left, "Controls");
    mvprintw(top + 10, left, "j = choose type + spawn");
    mvprintw(top + 11, left, "r/p/s = send REPICK");
    mvprintw(top + 12, left, "m = rematch");
    mvprintw(top + 13, left, "n = change player name");
    mvprintw(top + 14, left, "g = GET_STATE");
    mvprintw(top + 15, left, "q = quit");

    mvprintw(top + 17, left, "Players");
    int row = top + 18;

    for (int i = 0; i < MAX_PLAYERS && row < top + height - 1; i++)
    {
        const ViewPlayer *p = &ui->live_players[i];
        if (!p->present)
            continue;

        char shown_choice = sidebar_choice_symbol(ui, p);
        mvprintw(row++, left, "%-10s %c (%d,%d) %-5s",
                 p->name,
                 shown_choice,
                 (int)p->x,
                 (int)p->y,
                 player_status_text(p));
    }
}

static void draw_ui(const UiState *ui)
{
    int rows, cols;
    getmaxyx(stdscr, rows, cols);

    erase();

    if (rows < MIN_ROWS_STACKED || cols < MIN_COLS_STACKED)
    {
        mvprintw(0, 0, "Terminal too small. Resize to at least %dx%d.",
                 MIN_COLS_STACKED, MIN_ROWS_STACKED);
        refresh();
        return;
    }

    int board_w = board_render_width();
    int board_h = board_render_height();

    if (cols >= board_w + SIDEBAR_WIDTH + 8)
    {
        draw_board(ui, 1, 2);
        draw_sidebar(ui, 1, board_w + 6, rows - 2);
    }
    else
    {
        draw_board(ui, 1, 2);
        draw_sidebar(ui, board_h + 4, 2, rows - (board_h + 5));
    }

    refresh();
}

static int prompt_line(const char *prompt, char *out, size_t out_sz)
{
    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    (void)cols;

    echo();
    curs_set(1);
    nodelay(stdscr, FALSE);

    move(rows - 2, 0);
    clrtoeol();
    mvprintw(rows - 2, 0, "%s", prompt);

    move(rows - 1, 0);
    clrtoeol();
    refresh();

    int rc = getnstr(out, (int)out_sz - 1);

    noecho();
    curs_set(0);
    nodelay(stdscr, TRUE);

    move(rows - 2, 0);
    clrtoeol();
    move(rows - 1, 0);
    clrtoeol();

    return rc != ERR;
}

static int prompt_choice(char *out_choice)
{
    char buf[32];

    while (1)
    {
        if (!prompt_line("Enter R, P, or S: ", buf, sizeof(buf)))
        {
            return 0;
        }

        char c = (char)toupper((unsigned char)buf[0]);
        if (c == 'R' || c == 'P' || c == 'S')
        {
            *out_choice = c;
            return 1;
        }

        mvprintw(0, 0, "Invalid choice.");
        refresh();
    }
}

static int prompt_int(const char *prompt, int *out_value)
{
    char buf[32];
    int value;

    while (1)
    {
        if (!prompt_line(prompt, buf, sizeof(buf)))
        {
            return 0;
        }

        if (sscanf(buf, "%d", &value) == 1)
        {
            *out_value = value;
            return 1;
        }

        mvprintw(0, 0, "Invalid number.");
        refresh();
    }
}

static int prompt_name(char *out_name, size_t out_sz)
{
    while (1)
    {
        if (!prompt_line("Enter new player name: ", out_name, out_sz))
        {
            return 0;
        }

        if (out_name[0] == '\0')
        {
            continue;
        }

        if (strchr(out_name, ' ') != NULL)
        {
            mvprintw(0, 0, "Names cannot contain spaces.");
            refresh();
            continue;
        }

        return 1;
    }
}

static void send_join_attempt(UiState *ui)
{
    char choice;
    int spawn_x, spawn_y;

    if (!prompt_choice(&choice))
    {
        set_banner(ui, "Join cancelled");
        return;
    }

    if (!prompt_int("Enter spawn x: ", &spawn_x))
    {
        set_banner(ui, "Join cancelled");
        return;
    }

    if (!prompt_int("Enter spawn y: ", &spawn_y))
    {
        set_banner(ui, "Join cancelled");
        return;
    }

    (void)send_line(ui->fd, "CHOICE %c", choice);
    (void)send_line(ui->fd, "SPAWN %d %d", spawn_x, spawn_y);
    set_banner(ui, "Sent join attempt: %c at (%d,%d)", choice, spawn_x, spawn_y);
}

static void send_change_name(UiState *ui)
{
    char new_name[MAX_NAME_LENGTH];

    if (!prompt_name(new_name, sizeof(new_name)))
    {
        set_banner(ui, "Rename cancelled");
        return;
    }

    if (send_line(ui->fd, "CHANGE_NAME %s", new_name) < 0)
    {
        set_banner(ui, "Failed to send rename request");
        return;
    }

    set_banner(ui, "Requested rename to %s", new_name);
}

static void commit_snapshot(UiState *ui)
{
    memcpy(ui->live_players, ui->snapshot_players, sizeof(ui->live_players));
}

static void handle_player_line(UiState *ui, const char *line)
{
    char name[MAX_NAME_LENGTH];
    char choice;
    float x, y;
    int alive, waiting;

    if (sscanf(line, "PLAYER %31s %c %f %f %d %d",
               name, &choice, &x, &y, &alive, &waiting) == 6)
    {
        upsert_view_player(active_target_players(ui), name, choice, x, y, alive, waiting);
    }
}

static void handle_server_line(UiState *ui, const char *line)
{
    char name[MAX_NAME_LENGTH];
    char new_name[MAX_NAME_LENGTH];
    int id;
    long secs_left;
    int round_no;
    int round_secs;

    if (strcmp(line, "STATE_BEGIN") == 0)
    {
        ui->in_snapshot = 1;
        clear_view_players(ui->snapshot_players);
        return;
    }

    if (strcmp(line, "STATE_END") == 0)
    {
        commit_snapshot(ui);
        ui->in_snapshot = 0;
        return;
    }

    if (strncmp(line, "PLAYER ", 7) == 0)
    {
        handle_player_line(ui, line);
        return;
    }

    if (sscanf(line, "WELCOME %d", &id) == 1)
    {
        ui->self_id = id;
        snprintf(ui->self_name, sizeof(ui->self_name), "PLAYER%d", id);
        set_banner(ui, "WELCOME %d", id);
        return;
    }

    if (sscanf(line, "NAME_OK %31s", name) == 1)
    {
        snprintf(ui->self_name, sizeof(ui->self_name), "%s", name);
        set_banner(ui, "Your name is now %s", name);
        return;
    }

    if (sscanf(line, "RENAMED %31s %31s", name, new_name) == 2)
    {
        rename_view_player(ui->live_players, name, new_name);
        rename_view_player(ui->snapshot_players, name, new_name);

        if (strcmp(ui->self_name, name) == 0)
        {
            snprintf(ui->self_name, sizeof(ui->self_name), "%s", new_name);
        }

        set_banner(ui, "%s renamed to %s", name, new_name);
        return;
    }

    if (strcmp(line, "SPECTATING") == 0)
    {
        ui->phase = PHASE_LOBBY_OPEN;
        snprintf(ui->phase_line, sizeof(ui->phase_line), "Spectating");
        set_banner(ui, "%s", line);
        return;
    }

    if (strcmp(line, "LOBBY_WAITING") == 0)
    {
        ui->phase = PHASE_LOBBY_OPEN;
        snprintf(ui->phase_line, sizeof(ui->phase_line), "Lobby waiting for first join");
        set_banner(ui, "%s", line);
        return;
    }

    if (sscanf(line, "LOBBY_OPEN %ld", &secs_left) == 1)
    {
        ui->phase = PHASE_LOBBY_OPEN;
        snprintf(ui->phase_line, sizeof(ui->phase_line), "Lobby open (%ld s left)", secs_left);
        set_banner(ui, "%s", line);
        return;
    }

    if (strcmp(line, "LOBBY_CLOSED") == 0)
    {
        ui->phase = PHASE_SETUP;
        snprintf(ui->phase_line, sizeof(ui->phase_line), "Lobby closed");
        set_banner(ui, "%s", line);
        return;
    }

    if (sscanf(line, "SETUP_OPEN %d", &round_secs) == 1)
    {
        ui->phase = PHASE_SETUP;
        snprintf(ui->phase_line, sizeof(ui->phase_line), "Setup (%d s left)", round_secs);
        set_banner(ui, "%s", line);
        return;
    }

    if (sscanf(line, "ROUND_START %d %d", &round_no, &round_secs) == 2)
    {
        ui->phase = PHASE_ROUND_ACTIVE;
        snprintf(ui->phase_line, sizeof(ui->phase_line), "Round %d (%d s left)", round_no, round_secs);
        set_banner(ui, "%s", line);
        return;
    }

    if (strcmp(line, "REPICK_START") == 0)
    {
        ui->phase = PHASE_REPICK;
        snprintf(ui->phase_line, sizeof(ui->phase_line), "REPICK phase");
        set_banner(ui, "%s", line);
        return;
    }

    if (strcmp(line, "REPICK_DONE") == 0)
    {
        set_banner(ui, "%s", line);
        return;
    }

    if (sscanf(line, "GAME_OVER %31s", name) == 1)
    {
        ui->phase = PHASE_GAME_OVER;
        snprintf(ui->phase_line, sizeof(ui->phase_line), "Game over");
        set_banner(ui, "GAME_OVER winner=%s", name);
        return;
    }

    if (sscanf(line, "LEFT %31s", name) == 1)
    {
        remove_view_player(ui->live_players, name);
        remove_view_player(ui->snapshot_players, name);
        set_banner(ui, "%s", line);
        return;
    }

    if (strcmp(line, "MATCH_RESET") == 0)
    {
        ui->phase = PHASE_LOBBY_OPEN;
        clear_view_players(ui->live_players);
        clear_view_players(ui->snapshot_players);
        snprintf(ui->phase_line, sizeof(ui->phase_line), "Match reset");
        set_banner(ui, "%s", line);
        return;
    }

    set_banner(ui, "%s", line);
}

static int handle_keyboard(UiState *ui)
{
    int ch;

    while ((ch = getch()) != ERR)
    {
        if (ch == KEY_RESIZE)
        {
            continue;
        }

        if (ch < 0 || ch > UCHAR_MAX)
        {
            continue;
        }

        ch = toupper(ch);

        if (ch == 'Q')
        {
            (void)send_line(ui->fd, "QUIT");
            return 0;
        }
        else if (ch == 'J')
        {
            send_join_attempt(ui);
        }
        else if (ch == 'R' || ch == 'P' || ch == 'S')
        {
            (void)send_line(ui->fd, "REPICK %c", ch);
            set_banner(ui, "Sent REPICK %c", ch);
        }
        else if (ch == 'M')
        {
            (void)send_line(ui->fd, "REMATCH");
            set_banner(ui, "Sent REMATCH");
        }
        else if (ch == 'N')
        {
            send_change_name(ui);
        }
        else if (ch == 'G')
        {
            (void)send_line(ui->fd, "GET_STATE");
            set_banner(ui, "Requested state snapshot");
        }
    }

    return 1;
}

static void init_curses_ui(void)
{
    initscr();
    curses_started = 1;

    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    nodelay(stdscr, TRUE);
    curs_set(0);

    if (has_colors())
    {
        start_color();
        init_pair(1, COLOR_RED, COLOR_BLACK);
        init_pair(2, COLOR_GREEN, COLOR_BLACK);
        init_pair(3, COLOR_CYAN, COLOR_BLACK);
    }
}

int main(int argc, char **argv)
{
    const char *host = "127.0.0.1";

    if (argc >= 2)
    {
        host = argv[1];
    }

    if (net_init() != 0)
    {
        fatal("WSAStartup");
    }

    socket_t fd = connect_to_server(host, PORT);
    if (fd == INVALID_SOCKET)
    {
        fatal("connect");
    }

    UiState ui;
    memset(&ui, 0, sizeof(ui));
    ui.fd = fd;
    ui.net_player.fd = fd;

    snprintf(ui.self_name, sizeof(ui.self_name), "%s", "(assigning...)");
    ui.phase = PHASE_LOBBY_OPEN;
    snprintf(ui.phase_line, sizeof(ui.phase_line), "Connecting...");
    snprintf(ui.banner_line, sizeof(ui.banner_line), "Starting ncurses client");

    init_curses_ui();

    if (send_line(fd, "HELLO") < 0)
    {
        fatal("send HELLO");
    }

    (void)send_line(fd, "GET_STATE");
    set_banner(&ui, "Connected to %s", host);

    int running = 1;
    while (running)
    {
        draw_ui(&ui);

        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(fd, &readfds);

        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = UI_TICK_USEC;

        int ready = select((int)fd + 1, &readfds, NULL, NULL, &tv);
        if (ready < 0)
        {
            fatal("select");
        }

        if (ready == 0)
        {
            (void)send_line(ui.fd, "GET_STATE");
        }

        if (ready > 0 && FD_ISSET(fd, &readfds))
        {
            int rc = read_into_player_buffer(&ui.net_player);
            if (rc <= 0)
            {
                set_banner(&ui, "Disconnected from server");
                draw_ui(&ui);
                break;
            }

            char line[MAX_LINE];
            while (pop_line(&ui.net_player, line, sizeof(line)))
            {
                handle_server_line(&ui, line);
            }
        }

        running = handle_keyboard(&ui);
    }

    endwin();
    curses_started = 0;

    CLOSESOCKET(fd);
    net_cleanup();
    return 0;
}