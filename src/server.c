#include <stdarg.h>

#include "common.h"
#include "game.h"
#include "protocol.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#ifndef PORT
#define PORT 4242
#endif

static void fatal(const char *msg) {
    perror(msg);
    exit(1);
}

static int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) return -1;
    return 0;
}

static long seconds_left(time_t deadline) {
    long rem = (long)(deadline - time(NULL));
    return rem > 0 ? rem : 0;
}

static int find_name(ServerState *s, const char *name) {
    for (int i = 0; i < MAX_PLAYERS; i++) {
        Player *p = &s->players[i];
        if (p->connected && p->registered && strcmp(p->name, name) == 0) {
            return i;
        }
    }
    return -1;
}

static int spawn_taken(ServerState *s, int x, int y, int ignore_idx) {
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (i == ignore_idx) continue;

        Player *p = &s->players[i];
        if (!p->connected || !p->registered || !p->admitted) continue;
        if (!p->spawn_chosen) continue;

        if (p->x == x && p->y == y) {
            return 1;
        }
    }
    return 0;
}

static int all_alive_repicked(ServerState *s) {
    int alive = 0;

    for (int i = 0; i < MAX_PLAYERS; i++) {
        Player *p = &s->players[i];
        if (p->connected && p->registered && p->admitted && p->alive) {
            alive++;
            if (!p->repick_submitted) {
                return 0;
            }
        }
    }

    return alive > 0;
}

static int add_player(ServerState *s, int fd) {
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (!s->players[i].connected) {
            memset(&s->players[i], 0, sizeof(Player));
            s->players[i].fd = fd;
            s->players[i].id = s->next_id++;
            s->players[i].connected = 1;
            s->players[i].registered = 0;
            s->players[i].admitted = 0;
            s->players[i].alive = 0;
            s->players[i].waiting = 0;
            s->players[i].in_round = 0;
            s->players[i].choice_chosen = 0;
            s->players[i].spawn_chosen = 0;
            s->players[i].repick_submitted = 0;
            s->players[i].repick_choice = 0;
            s->players[i].x = -1;
            s->players[i].y = -1;
            s->players[i].inbuf_used = 0;
            s->players[i].outbuf_used = 0;
            return i;
        }
    }
    return -1;
}

static void drop_player(ServerState *s, int idx, int announce) {
    char name[MAX_NAME];
    int should_announce = 0;

    if (!s->players[idx].connected) {
        return;
    }

    if (announce && s->players[idx].registered) {
        snprintf(name, sizeof(name), "%s", s->players[idx].name);
        should_announce = 1;
    }

    close(s->players[idx].fd);
    memset(&s->players[idx], 0, sizeof(Player));
    s->players[idx].x = -1;
    s->players[idx].y = -1;

    if (should_announce) {
        char line[MAX_LINE];
        snprintf(line, sizeof(line), "LEFT %s", name);

        for (int i = 0; i < MAX_PLAYERS; i++) {
            if (s->players[i].connected) {
                (void)queue_line(&s->players[i], "%s", line);
            }
        }
    }
}

static void queue_broadcast(ServerState *s, const char *fmt, ...) {
    char line[MAX_LINE];
    int to_drop[MAX_PLAYERS];
    int drop_count = 0;

    va_list args;
    va_start(args, fmt);
    vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);

    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (!s->players[i].connected) continue;

        if (queue_line(&s->players[i], "%s", line) < 0) {
            to_drop[drop_count++] = i;
        }
    }

    for (int k = 0; k < drop_count; k++) {
        drop_player(s, to_drop[k], 0);
    }
}

static void broadcast_positions(ServerState *s) {
    for (int i = 0; i < MAX_PLAYERS; i++) {
        Player *p = &s->players[i];
        if (p->connected && p->registered && p->admitted) {
            queue_broadcast(s, "PLAYER %s %c %d %d %d %d",
                            p->name,
                            p->choice_chosen ? p->choice : '?',
                            p->x,
                            p->y,
                            p->alive,
                            p->waiting);
        }
    }
}

static void start_active_round(ServerState *s) {
    start_round(s);
    queue_broadcast(s, "ROUND_START %d %d", s->round_no, ROUND_SECONDS);
    broadcast_positions(s);
}

static void finish_repicks(ServerState *s) {
    for (int i = 0; i < MAX_PLAYERS; i++) {
        Player *p = &s->players[i];
        if (p->connected && p->registered && p->admitted && p->alive) {
            p->choice = p->repick_choice;
            p->repick_submitted = 0;
            p->repick_choice = 0;
        }
    }

    queue_broadcast(s, "REPICK_DONE");
    broadcast_positions(s);
    start_active_round(s);
}

static void begin_repicks(ServerState *s) {
    s->phase = PHASE_REPICK;

    for (int i = 0; i < MAX_PLAYERS; i++) {
        Player *p = &s->players[i];
        p->in_round = 0;
        p->repick_submitted = 0;
        p->repick_choice = 0;
    }

    queue_broadcast(s, "REPICK_START");
    broadcast_positions(s);
}

static void reset_match(ServerState *s) {
    s->phase = PHASE_LOBBY_OPEN;
    s->join_deadline = 0;
    s->setup_deadline = 0;
    s->round_deadline = 0;
    s->round_no = 0;

    for (int i = 0; i < MAX_PLAYERS; i++) {
        Player *p = &s->players[i];
        if (!p->connected || !p->registered) continue;

        p->admitted = 0;
        p->alive = 0;
        p->waiting = 0;
        p->in_round = 0;

        p->choice_chosen = 0;
        p->spawn_chosen = 0;

        p->repick_submitted = 0;
        p->repick_choice = 0;

        p->choice = 0;
        p->x = -1;
        p->y = -1;

        (void)queue_line(p, "MATCH_RESET");
        (void)queue_line(p, "SPECTATING");
        (void)queue_line(p, "CHOOSE_TYPE");
        (void)queue_line(p, "CHOOSE_SPAWN %d %d", GRID_W, GRID_H);
        (void)queue_line(p, "LOBBY_WAITING");
    }
}

static void end_game(ServerState *s) {
    s->phase = PHASE_GAME_OVER;

    for (int i = 0; i < MAX_PLAYERS; i++) {
        Player *p = &s->players[i];
        if (p->connected && p->registered && p->admitted && p->alive) {
            queue_broadcast(s, "GAME_OVER %s", p->name);
            return;
        }
    }

    queue_broadcast(s, "GAME_OVER nobody");
}

static void reevaluate_state(ServerState *s) {
    if (s->phase == PHASE_GAME_OVER) return;

    if (s->phase == PHASE_LOBBY_OPEN) {
        if (admitted_count(s) == 0) {
            s->join_deadline = 0;
        }
        return;
    }

    if (s->phase == PHASE_SETUP) {
        if (admitted_count(s) < 2) {
            end_game(s);
            return;
        }
        if (all_admitted_ready(s)) {
            start_active_round(s);
        }
        return;
    }

    if (s->phase == PHASE_REPICK) {
        if (active_alive_count(s) <= 1) {
            end_game(s);
            return;
        }
        if (all_alive_repicked(s)) {
            finish_repicks(s);
        }
        return;
    }

    if (s->phase == PHASE_ROUND_ACTIVE) {
        if (active_alive_count(s) <= 1) {
            end_game(s);
        }
    }
}

static void close_lobby_if_needed(ServerState *s) {
    if (s->phase != PHASE_LOBBY_OPEN) return;
    if (s->join_deadline == 0) return;
    if (time(NULL) < s->join_deadline) return;

    s->phase = PHASE_SETUP;
    s->setup_deadline = time(NULL) + SETUP_SECONDS;

    queue_broadcast(s, "LOBBY_CLOSED");
    queue_broadcast(s, "SETUP_OPEN %d", SETUP_SECONDS);

    if (admitted_count(s) < 2) {
        end_game(s);
        return;
    }

    if (all_admitted_ready(s)) {
        start_active_round(s);
    } else {
        queue_broadcast(s, "WAITING_FOR_OTHERS");
    }
}

static void expire_unready_setup_players(ServerState *s) {
    if (s->phase != PHASE_SETUP) return;
    if (s->setup_deadline == 0) return;
    if (time(NULL) < s->setup_deadline) return;

    for (int i = 0; i < MAX_PLAYERS; i++) {
        Player *p = &s->players[i];
        if (p->connected && p->registered && p->admitted) {
            if (!p->choice_chosen || !p->spawn_chosen) {
                drop_player(s, i, 1);
            }
        }
    }

    if (admitted_count(s) < 2) {
        end_game(s);
        return;
    }

    if (all_admitted_ready(s)) {
        start_active_round(s);
    }
}

static void maybe_admit_player(ServerState *s, int idx) {
    Player *p = &s->players[idx];

    if (!p->connected || !p->registered) return;
    if (p->admitted) return;
    if (!p->choice_chosen || !p->spawn_chosen) return;

    if (s->phase != PHASE_LOBBY_OPEN) {
        (void)queue_line(p, "ERROR lobby_closed");
        return;
    }

    if (s->join_deadline == 0) {
        s->join_deadline = time(NULL) + JOIN_WINDOW_SECONDS;
        queue_broadcast(s, "LOBBY_OPEN %d", JOIN_WINDOW_SECONDS);
    } else if (time(NULL) >= s->join_deadline) {
        (void)queue_line(p, "ERROR lobby_closed");
        return;
    }

    p->admitted = 1;
    p->waiting = 1;

    (void)queue_line(p, "JOINED_MATCH");
    queue_broadcast(s, "JOINED %s", p->name);
    broadcast_positions(s);
}

static void resolve_round(ServerState *s) {
    Pair pairs[MAX_PLAYERS / 2];
    int bye_index = -1;
    int pair_count = build_pairs(s, pairs, MAX_PLAYERS / 2, &bye_index);

    for (int k = 0; k < pair_count; k++) {
        Player *a = &s->players[pairs[k].a];
        Player *b = &s->players[pairs[k].b];

        int ax = a->x;
        int ay = a->y;
        int bx = b->x;
        int by = b->y;

        int r = rps_result(a->choice, b->choice);

        if (r == 1) {
            a->x = bx;
            a->y = by;

            b->alive = 0;
            b->in_round = 0;
            b->x = -1;
            b->y = -1;

            queue_broadcast(s, "PAIR %s %s %c %c WINNER %s MOVE %d %d",
                            a->name, b->name, a->choice, b->choice,
                            a->name, a->x, a->y);
            if (b->connected) {
                (void)queue_line(b, "ELIMINATED lost");
            }
        } else if (r == -1) {
            b->x = ax;
            b->y = ay;

            a->alive = 0;
            a->in_round = 0;
            a->x = -1;
            a->y = -1;

            queue_broadcast(s, "PAIR %s %s %c %c WINNER %s MOVE %d %d",
                            a->name, b->name, a->choice, b->choice,
                            b->name, b->x, b->y);
            if (a->connected) {
                (void)queue_line(a, "ELIMINATED lost");
            }
        } else {
            queue_broadcast(s, "PAIR %s %s %c %c TIE",
                            a->name, b->name, a->choice, b->choice);
        }
    }

    if (bye_index != -1) {
        queue_broadcast(s, "BYE %s", s->players[bye_index].name);
    }

    for (int i = 0; i < MAX_PLAYERS; i++) {
        s->players[i].in_round = 0;
    }

    broadcast_positions(s);

    if (active_alive_count(s) <= 1) {
        end_game(s);
        return;
    }

    if (all_alive_same_choice(s)) {
        begin_repicks(s);
        return;
    }

    start_active_round(s);
}

static void handle_command(ServerState *s, int idx, const char *line) {
    Player *p = &s->players[idx];

    if (strncmp(line, "HELLO ", 6) == 0) {
        char name[MAX_NAME];

        if (p->registered) {
            (void)queue_line(p, "ERROR already_registered");
            return;
        }

        if (sscanf(line + 6, "%31s", name) != 1) {
            (void)queue_line(p, "ERROR usage_HELLO_name");
            return;
        }

        if (find_name(s, name) != -1) {
            (void)queue_line(p, "ERROR duplicate_name");
            return;
        }

        snprintf(p->name, sizeof(p->name), "%s", name);
        p->registered = 1;
        p->admitted = 0;
        p->alive = 0;
        p->waiting = 0;
        p->in_round = 0;
        p->choice_chosen = 0;
        p->spawn_chosen = 0;
        p->x = -1;
        p->y = -1;

        (void)queue_line(p, "WELCOME %d", p->id);
        (void)queue_line(p, "SPECTATING");

        if (s->phase == PHASE_LOBBY_OPEN) {
            (void)queue_line(p, "CHOOSE_TYPE");
            (void)queue_line(p, "CHOOSE_SPAWN %d %d", GRID_W, GRID_H);

            if (s->join_deadline == 0) {
                (void)queue_line(p, "LOBBY_WAITING");
            } else {
                (void)queue_line(p, "LOBBY_OPEN %ld", seconds_left(s->join_deadline));
            }
        } else {
            (void)queue_line(p, "LOBBY_CLOSED");
        }

        return;
    }

    if (strncmp(line, "CHOICE ", 7) == 0) {
        char choice = line[7];

        if (!p->registered) {
            (void)queue_line(p, "ERROR register_first");
            return;
        }

        if (p->admitted) {
            (void)queue_line(p, "ERROR already_joined");
            return;
        }

        if (choice >= 'a' && choice <= 'z') {
            choice = (char)(choice - 'a' + 'A');
        }

        if (choice != 'R' && choice != 'P' && choice != 'S') {
            (void)queue_line(p, "ERROR bad_choice");
            return;
        }

        p->choice = choice;
        p->choice_chosen = 1;
        (void)queue_line(p, "CHOICE_OK %c", choice);

        maybe_admit_player(s, idx);
        return;
    }

    if (strncmp(line, "SPAWN ", 6) == 0) {
        int x, y;

        if (!p->registered) {
            (void)queue_line(p, "ERROR register_first");
            return;
        }

        if (p->admitted) {
            (void)queue_line(p, "ERROR already_joined");
            return;
        }

        if (sscanf(line + 6, "%d %d", &x, &y) != 2) {
            (void)queue_line(p, "ERROR usage_SPAWN_x_y");
            return;
        }

        if (x < 0 || x >= GRID_W || y < 0 || y >= GRID_H) {
            (void)queue_line(p, "ERROR bad_spawn");
            return;
        }

        if (spawn_taken(s, x, y, idx)) {
            (void)queue_line(p, "ERROR spawn_taken");
            return;
        }

        p->x = x;
        p->y = y;
        p->spawn_chosen = 1;
        (void)queue_line(p, "SPAWN_OK %d %d", x, y);

        maybe_admit_player(s, idx);
        return;
    }

    if (strncmp(line, "REPICK ", 7) == 0) {
        char choice = line[7];

        if (choice >= 'a' && choice <= 'z') {
            choice = (char)(choice - 'a' + 'A');
        }

        if (s->phase != PHASE_REPICK) {
            (void)queue_line(p, "ERROR not_in_repick_phase");
            return;
        }

        if (!p->registered || !p->admitted || !p->alive) {
            (void)queue_line(p, "ERROR not_alive");
            return;
        }

        if (p->repick_submitted) {
            (void)queue_line(p, "ERROR already_repicked");
            return;
        }

        if (choice != 'R' && choice != 'P' && choice != 'S') {
            (void)queue_line(p, "ERROR bad_choice");
            return;
        }

        p->repick_choice = choice;
        p->repick_submitted = 1;
        (void)queue_line(p, "REPICK_OK %c", choice);

        if (all_alive_repicked(s)) {
            finish_repicks(s);
        } else {
            (void)queue_line(p, "REPICK_WAITING");
        }
        return;
    }

    if (strcmp(line, "REMATCH") == 0) {
        if (s->phase != PHASE_GAME_OVER) {
            (void)queue_line(p, "ERROR rematch_not_available");
            return;
        }

        reset_match(s);
        return;
    }

    if (strcmp(line, "QUIT") == 0) {
        drop_player(s, idx, 1);
        reevaluate_state(s);
        return;
    }

    (void)queue_line(p, "ERROR bad_command");
}

int main(void) {
    signal(SIGPIPE, SIG_IGN);

    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) fatal("socket");

    int yes = 1;
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0) {
        fatal("setsockopt");
    }

    if (set_nonblocking(listen_fd) < 0) {
        fatal("fcntl");
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) fatal("bind");
    if (listen(listen_fd, 16) < 0) fatal("listen");

    ServerState state;
    init_server_state(&state);

    printf("Server listening on port %d\n", PORT);

    while (1) {
        fd_set readfds, writefds;
        FD_ZERO(&readfds);
        FD_ZERO(&writefds);

        FD_SET(listen_fd, &readfds);
        int maxfd = listen_fd;

        for (int i = 0; i < MAX_PLAYERS; i++) {
            if (state.players[i].connected) {
                FD_SET(state.players[i].fd, &readfds);

                if (player_has_pending_output(&state.players[i])) {
                    FD_SET(state.players[i].fd, &writefds);
                }

                if (state.players[i].fd > maxfd) {
                    maxfd = state.players[i].fd;
                }
            }
        }

        struct timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;

        int ready = select(maxfd + 1, &readfds, &writefds, NULL, &tv);
        if (ready < 0) {
            if (errno == EINTR) continue;
            fatal("select");
        }

        if (FD_ISSET(listen_fd, &readfds)) {
            int client_fd = accept(listen_fd, NULL, NULL);
            if (client_fd >= 0) {
                if (set_nonblocking(client_fd) < 0) {
                    close(client_fd);
                } else {
                    int idx = add_player(&state, client_fd);
                    if (idx >= 0) {
                        if (queue_line(&state.players[idx], "INFO connected_send_HELLO_name") < 0) {
                            drop_player(&state, idx, 0);
                        }
                    } else {
                        (void)send_line(client_fd, "ERROR server_full");
                        close(client_fd);
                    }
                }
            }
        }

        for (int i = 0; i < MAX_PLAYERS; i++) {
            if (!state.players[i].connected) continue;

            if (FD_ISSET(state.players[i].fd, &readfds)) {
                int rc = read_into_player_buffer(&state.players[i]);
                if (rc <= 0) {
                    drop_player(&state, i, 1);
                    reevaluate_state(&state);
                    continue;
                }

                char line[MAX_LINE];
                while (pop_line(&state.players[i], line, sizeof(line))) {
                    handle_command(&state, i, line);
                }
            }
        }

        for (int i = 0; i < MAX_PLAYERS; i++) {
            if (!state.players[i].connected) continue;

            if (FD_ISSET(state.players[i].fd, &writefds)) {
                int rc = flush_player_output(&state.players[i]);
                if (rc < 0) {
                    drop_player(&state, i, 1);
                    reevaluate_state(&state);
                }
            }
        }

        close_lobby_if_needed(&state);
        expire_unready_setup_players(&state);

        if (state.phase == PHASE_ROUND_ACTIVE &&
            time(NULL) >= state.round_deadline) {
            resolve_round(&state);
        }
    }

    return 0;
}