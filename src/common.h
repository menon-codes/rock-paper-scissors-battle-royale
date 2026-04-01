#ifndef COMMON_H
#define COMMON_H

#include <stddef.h>
#include <time.h>

#define MAX_PLAYERS 32
#define MAX_NAME 32
#define MAX_LINE 256
#define INBUF_SIZE 1024
#define OUTBUF_SIZE 8192

#define GRID_W 10
#define GRID_H 10

#define JOIN_WINDOW_SECONDS 60
#define SETUP_SECONDS 20
#define ROUND_SECONDS 5

typedef enum {
    PHASE_LOBBY_OPEN,
    PHASE_SETUP,
    PHASE_ROUND_ACTIVE,
    PHASE_REPICK,
    PHASE_GAME_OVER
} Phase;

typedef struct {
    int fd;
    int id;

    int connected;
    int registered;
    int admitted;

    int alive;
    int waiting;
    int in_round;

    int choice_chosen;
    int spawn_chosen;

    int repick_submitted;
    char repick_choice;

    char name[MAX_NAME];
    char choice;

    int x;
    int y;

    char inbuf[INBUF_SIZE];
    size_t inbuf_used;

    char outbuf[OUTBUF_SIZE];
    size_t outbuf_used;
} Player;

typedef struct {
    Player players[MAX_PLAYERS];
    int next_id;
    int round_no;
    Phase phase;
    time_t join_deadline;
    time_t setup_deadline;
    time_t round_deadline;
} ServerState;

typedef struct {
    int a;
    int b;
} Pair;

#endif