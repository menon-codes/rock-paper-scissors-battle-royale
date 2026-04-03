#ifndef COMMON_H
#define COMMON_H

/*
 * Cross-platform definitions shared by client and server binaries.
 *
 * This header centralizes:
 * - socket compatibility wrappers for Windows and POSIX;
 * - global protocol/game constants;
 * - canonical game/server state structs used across modules.
 */

#if defined(_WIN32) || defined(_WIN64) || defined(_MSC_VER) || defined(__MINGW32__) || defined(__MINGW64__)
#define RPS_WINDOWS_SOCKETS 1
#elif defined(__has_include)
#if __has_include(<winsock2.h>)
#define RPS_WINDOWS_SOCKETS 1
#else
#define RPS_WINDOWS_SOCKETS 0
#endif
#else
#define RPS_WINDOWS_SOCKETS 0
#endif

#if RPS_WINDOWS_SOCKETS
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef NOGDI
#define NOGDI
#endif
#ifndef NOUSER
#define NOUSER
#endif
#ifndef NOMMSYSTEM
#define NOMMSYSTEM
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <stddef.h>
#include <time.h>

#define MAX_PLAYERS 32
#define MAX_NAME_LENGTH 32
#define MAX_LINE 256
#define INBUF_SIZE 1024
#define OUTBUF_SIZE 8192

#define GRID_W 10
#define GRID_H 10

#define JOIN_WINDOW_SECONDS 60
#define SETUP_SECONDS 20
#define ROUND_SECONDS 5

#if RPS_WINDOWS_SOCKETS
typedef SOCKET socket_t;
#define CLOSESOCKET closesocket
#define NET_LAST_ERROR() WSAGetLastError()
#define NET_WOULD_BLOCK(e) ((e) == WSAEWOULDBLOCK)
#define NET_INTERRUPTED(e) ((e) == WSAEINTR)
#ifndef STDIN_FILENO
#define STDIN_FILENO 0
#endif
#else
typedef int socket_t;
#define INVALID_SOCKET ((socket_t) - 1)
#define CLOSESOCKET close
#define NET_LAST_ERROR() errno
#define NET_WOULD_BLOCK(e) ((e) == EAGAIN || (e) == EWOULDBLOCK)
#define NET_INTERRUPTED(e) ((e) == EINTR)
#endif

static inline int net_init(void)
{
#if RPS_WINDOWS_SOCKETS
    WSADATA wsa_data;
    return WSAStartup(MAKEWORD(2, 2), &wsa_data);
#else
    return 0;
#endif
}

static inline void net_cleanup(void)
{
#if RPS_WINDOWS_SOCKETS
    WSACleanup();
#endif
}

static inline int net_set_nonblocking(socket_t fd)
{
#if RPS_WINDOWS_SOCKETS
    u_long mode = 1;
    return ioctlsocket(fd, FIONBIO, &mode);
#else
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0)
        return -1;
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
        return -1;
    return 0;
#endif
}

typedef enum
{
    /* Waiting for players to submit CHOICE/SPAWN and be admitted. */
    PHASE_LOBBY_OPEN,
    /* Lobby closed; admitted players are finalizing setup. */
    PHASE_SETUP,
    /* Active elimination round in progress. */
    PHASE_ROUND_ACTIVE,
    /* Tie breaker phase where alive players re-pick R/P/S. */
    PHASE_REPICK,
    /* Match ended; rematch may reset back to lobby. */
    PHASE_GAME_OVER
} Phase;

typedef struct
{
    /* Connection identity. */
    socket_t fd;
    int id;

    /* Lifecycle flags. */
    int connected;
    int registered;
    int admitted;

    /* Round participation flags. */
    int alive;
    int waiting;
    int in_round;

    /* Setup and choice state. */
    int choice_chosen;
    int spawn_chosen;

    /* REPICK-only transient state. */
    int repick_submitted;
    char repick_choice;

    /* Public gameplay identity/state. */
    char name[MAX_NAME_LENGTH];
    char choice;

    float x;
    float y;

    /* Per-player line-oriented network buffers. */
    char inbuf[INBUF_SIZE];
    size_t inbuf_used;

    char outbuf[OUTBUF_SIZE];
    size_t outbuf_used;
} Player;

typedef struct
{
    /* Fixed slots; empty slot has connected == 0. */
    Player players[MAX_PLAYERS];

    /* Monotonic identifier assigned when a player slot is created. */
    int next_id;

    /* Match progression metadata. */
    int round_no;
    Phase phase;

    /* Server timers (epoch seconds). */
    time_t join_deadline;
    time_t setup_deadline;
    time_t round_deadline;
} ServerState;

typedef struct
{
    /* Indexes into ServerState.players for an RPS matchup pair. */
    int a;
    int b;
} Pair;

#endif