#include "common.h"
#include "protocol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if RPS_WINDOWS_SOCKETS
#include <process.h>
#include <windows.h>
#else
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#endif

#ifndef PORT
#define PORT 4242
#endif

static int g_tests_run = 0;
static int g_tests_failed = 0;

#define CHECK(cond)                                                         \
	do                                                                      \
	{                                                                       \
		g_tests_run++;                                                      \
		if (!(cond))                                                        \
		{                                                                   \
			g_tests_failed++;                                               \
			fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
		}                                                                   \
	} while (0)

typedef struct
{
	socket_t fd;
	Player io;
} TestClient;

#if RPS_WINDOWS_SOCKETS
typedef intptr_t server_pid_t;
#else
typedef pid_t server_pid_t;
#endif

static void sleep_ms(int ms)
{
#if RPS_WINDOWS_SOCKETS
	Sleep((DWORD)ms);
#else
	struct timespec ts;
	ts.tv_sec = ms / 1000;
	ts.tv_nsec = (long)(ms % 1000) * 1000000L;
	nanosleep(&ts, NULL);
#endif
}

static server_pid_t launch_server_process(void)
{
#if RPS_WINDOWS_SOCKETS
	return _spawnl(_P_NOWAIT, "build/bin/server.exe", "build/bin/server.exe", NULL);
#else
	pid_t pid = fork();
	if (pid == 0)
	{
		execl("build/bin/server", "build/bin/server", (char *)NULL);
		_exit(127);
	}
	return pid;
#endif
}

static void stop_server_process(server_pid_t pid)
{
#if RPS_WINDOWS_SOCKETS
	if (pid <= 0)
	{
		return;
	}

	HANDLE h = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, (DWORD)pid);
	if (h != NULL)
	{
		TerminateProcess(h, 0);
		WaitForSingleObject(h, 3000);
		CloseHandle(h);
	}
#else
	if (pid <= 0)
	{
		return;
	}

	kill(pid, SIGTERM);
	waitpid(pid, NULL, 0);
#endif
}

static void init_client_io(TestClient *c)
{
	memset(&c->io, 0, sizeof(c->io));
	c->io.fd = c->fd;
}

static int recv_line_timeout(TestClient *c, char *out, size_t out_sz, int timeout_ms)
{
	int elapsed = 0;
	const int step_ms = 100;

	while (elapsed <= timeout_ms)
	{
		if (pop_line(&c->io, out, out_sz))
		{
			return 1;
		}

		fd_set readfds;
		FD_ZERO(&readfds);
		FD_SET(c->fd, &readfds);

		struct timeval tv;
		tv.tv_sec = step_ms / 1000;
		tv.tv_usec = (step_ms % 1000) * 1000;

		int rc = select((int)c->fd + 1, &readfds, NULL, NULL, &tv);
		if (rc < 0)
		{
			if (NET_INTERRUPTED(NET_LAST_ERROR()))
			{
				continue;
			}
			return -1;
		}

		if (rc == 0)
		{
			elapsed += step_ms;
			continue;
		}

		int rr = read_into_player_buffer(&c->io);
		if (rr <= 0)
		{
			return -1;
		}
	}

	return 0;
}

static int wait_for_substring(TestClient *c, const char *needle, int timeout_ms)
{
	char line[MAX_LINE];
	int elapsed = 0;
	const int step = 250;

	while (elapsed <= timeout_ms)
	{
		int rc = recv_line_timeout(c, line, sizeof(line), step);
		if (rc < 0)
		{
			return -1;
		}
		if (rc == 0)
		{
			elapsed += step;
			continue;
		}

		if (strstr(line, needle) != NULL)
		{
			return 1;
		}
	}

	return 0;
}

static int connect_client(TestClient *c)
{
	c->fd = connect_to_server("127.0.0.1", PORT);
	if (c->fd == INVALID_SOCKET)
	{
		return 0;
	}
	init_client_io(c);
	return 1;
}

static void disconnect_client(TestClient *c)
{
	if (c->fd != INVALID_SOCKET)
	{
		CLOSESOCKET(c->fd);
		c->fd = INVALID_SOCKET;
	}
}

static void try_quit(TestClient *c)
{
	if (c->fd != INVALID_SOCKET)
	{
		(void)send_line(c->fd, "QUIT");
	}
}

static void test_handshake_and_duplicate_name(void)
{
	TestClient a;
	TestClient b;

	CHECK(connect_client(&a));
	CHECK(connect_client(&b));

	CHECK(wait_for_substring(&a, "INFO connected_send_HELLO_name", 2000) == 1);
	CHECK(wait_for_substring(&b, "INFO connected_send_HELLO_name", 2000) == 1);

	CHECK(send_line(a.fd, "HELLO alice") == 0);
	CHECK(wait_for_substring(&a, "WELCOME", 2000) == 1);

	CHECK(send_line(b.fd, "HELLO alice") == 0);
	CHECK(wait_for_substring(&b, "ERROR duplicate_name", 2000) == 1);

	try_quit(&a);
	try_quit(&b);
	disconnect_client(&a);
	disconnect_client(&b);
}

static void test_join_and_left_broadcast(void)
{
	TestClient a;
	TestClient b;

	CHECK(connect_client(&a));
	CHECK(connect_client(&b));

	CHECK(wait_for_substring(&a, "INFO connected_send_HELLO_name", 2000) == 1);
	CHECK(wait_for_substring(&b, "INFO connected_send_HELLO_name", 2000) == 1);

	CHECK(send_line(a.fd, "HELLO alpha") == 0);
	CHECK(send_line(b.fd, "HELLO beta") == 0);
	CHECK(wait_for_substring(&a, "WELCOME", 2000) == 1);
	CHECK(wait_for_substring(&b, "WELCOME", 2000) == 1);

	CHECK(send_line(a.fd, "CHOICE R") == 0);
	CHECK(send_line(a.fd, "SPAWN 1 1") == 0);
	CHECK(wait_for_substring(&a, "JOINED_MATCH", 2000) == 1);
	CHECK(wait_for_substring(&b, "JOINED alpha", 2000) == 1);

	CHECK(send_line(a.fd, "QUIT") == 0);
	CHECK(wait_for_substring(&b, "LEFT alpha", 2000) == 1);

	try_quit(&b);
	disconnect_client(&a);
	disconnect_client(&b);
}

static void test_get_state_and_repick_error(void)
{
	TestClient c;

	CHECK(connect_client(&c));
	CHECK(wait_for_substring(&c, "INFO connected_send_HELLO_name", 2000) == 1);

	CHECK(send_line(c.fd, "HELLO gamma") == 0);
	CHECK(wait_for_substring(&c, "WELCOME", 2000) == 1);

	CHECK(send_line(c.fd, "GET_STATE") == 0);
	CHECK(wait_for_substring(&c, "STATE_BEGIN", 2000) == 1);
	CHECK(wait_for_substring(&c, "STATE_END", 2000) == 1);

	CHECK(send_line(c.fd, "REPICK R") == 0);
	CHECK(wait_for_substring(&c, "ERROR not_in_repick_phase", 2000) == 1);

	try_quit(&c);
	disconnect_client(&c);
}

int main(void)
{
	if (net_init() != 0)
	{
		fprintf(stderr, "net_init failed\n");
		return 1;
	}

	server_pid_t pid = launch_server_process();
	CHECK(pid > 0);

	/* Give server time to bind/listen before connecting clients. */
	sleep_ms(500);

	test_handshake_and_duplicate_name();
	test_join_and_left_broadcast();
	test_get_state_and_repick_error();

	stop_server_process(pid);
	net_cleanup();

	if (g_tests_failed == 0)
	{
		printf("Integration tests passed (%d checks).\n", g_tests_run);
		return 0;
	}

	printf("Integration tests failed: %d of %d checks.\n", g_tests_failed, g_tests_run);
	return 1;
}
