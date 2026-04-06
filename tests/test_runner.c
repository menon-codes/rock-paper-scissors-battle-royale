#include "common.h"
#include "game.h"
#include "protocol.h"
#include "server_commands.h"
#include "server_state.h"
#include "chase_simulation.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

static int g_tests_run = 0;
static int g_tests_failed = 0;

#define TEST_CHASE_DT 0.05f

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

static int contains_bytes(const char *buf, size_t len, const char *needle)
{
	size_t n = strlen(needle);
	if (n == 0 || n > len)
	{
		return 0;
	}

	for (size_t i = 0; i + n <= len; i++)
	{
		if (memcmp(buf + i, needle, n) == 0)
		{
			return 1;
		}
	}
	return 0;
}

static int player_out_contains(const Player *p, const char *needle)
{
	return contains_bytes(p->outbuf, p->outbuf_used, needle);
}

static void init_connected_player(Player *p, int id)
{
	memset(p, 0, sizeof(*p));
	p->connected = 1;
	p->fd = INVALID_SOCKET;
	p->id = id;
	p->x = -1.0f;
	p->y = -1.0f;
}

static void clear_player_output(Player *p)
{
	p->outbuf_used = 0;
}

static void setup_registered_admitted_player(Player *p, int id, const char *name, float x, float y, char choice)
{
	init_connected_player(p, id);
	snprintf(p->name, sizeof(p->name), "%s", name);
	p->registered = 1;
	p->admitted = 1;
	p->choice_chosen = 1;
	p->spawn_chosen = 1;
	p->alive = 1;
	p->waiting = 0;
	p->in_round = 1;
	p->x = x;
	p->y = y;
	p->choice = choice;
}

static void test_protocol_queue_and_pop(void)
{
	Player p;
	init_connected_player(&p, 1);

	CHECK(queue_line(&p, "HELLO %s", "alice") == 0);
	CHECK(p.outbuf_used > 0);
	CHECK(player_out_contains(&p, "HELLO alice\n"));

	p.inbuf_used = 0;
	memcpy(p.inbuf, "A\r\nB\n", 5);
	p.inbuf_used = 5;

	char line[MAX_LINE];
	CHECK(pop_line(&p, line, sizeof(line)) == 1);
	CHECK(strcmp(line, "A") == 0);
	CHECK(pop_line(&p, line, sizeof(line)) == 1);
	CHECK(strcmp(line, "B") == 0);
	CHECK(pop_line(&p, line, sizeof(line)) == 0);
}

static void test_protocol_queue_overflow(void)
{
	Player p;
	init_connected_player(&p, 1);
	p.outbuf_used = OUTBUF_SIZE - 1;
	CHECK(queue_line(&p, "X") == -1);
}

static void test_game_core_logic(void)
{
	CHECK(rps_result('R', 'S') == 1);
	CHECK(rps_result('S', 'P') == 1);
	CHECK(rps_result('P', 'R') == 1);
	CHECK(rps_result('R', 'P') == -1);
	CHECK(rps_result('S', 'R') == -1);
	CHECK(rps_result('P', 'P') == 0);

	ServerState s;
	init_server_state(&s);

	init_connected_player(&s.players[0], 11);
	init_connected_player(&s.players[1], 12);
	s.players[0].registered = 1;
	s.players[1].registered = 1;
	s.players[0].admitted = 1;
	s.players[1].admitted = 1;
	s.players[0].choice_chosen = 1;
	s.players[1].choice_chosen = 1;
	s.players[0].spawn_chosen = 1;
	s.players[1].spawn_chosen = 1;
	s.players[0].alive = 1;
	s.players[1].alive = 1;
	s.players[0].choice = 'R';
	s.players[1].choice = 'R';

	CHECK(all_admitted_ready(&s) == 1);
	CHECK(all_alive_same_choice(&s) == 1);

	s.players[1].choice = 'P';
	CHECK(all_alive_same_choice(&s) == 0);
}

static void test_build_pairs_nearest_neighbors(void)
{
	ServerState s;
	init_server_state(&s);

	init_connected_player(&s.players[0], 1);
	init_connected_player(&s.players[1], 2);
	init_connected_player(&s.players[2], 3);
	init_connected_player(&s.players[3], 4);

	for (int i = 0; i < 4; i++)
	{
		s.players[i].registered = 1;
		s.players[i].admitted = 1;
		s.players[i].alive = 1;
		s.players[i].in_round = 1;
	}

	s.players[0].x = 0;
	s.players[0].y = 0;
	s.players[1].x = 1;
	s.players[1].y = 0;
	s.players[2].x = 9;
	s.players[2].y = 9;
	s.players[3].x = 8;
	s.players[3].y = 9;

	Pair pairs[MAX_PLAYERS / 2];
	int bye = -1;
	int n = build_pairs(&s, pairs, MAX_PLAYERS / 2, &bye);

	CHECK(n == 2);
	CHECK(bye == -1);

	int has01 = 0;
	int has23 = 0;
	for (int i = 0; i < n; i++)
	{
		int a = pairs[i].a;
		int b = pairs[i].b;
		if ((a == 0 && b == 1) || (a == 1 && b == 0))
		{
			has01 = 1;
		}
		if ((a == 2 && b == 3) || (a == 3 && b == 2))
		{
			has23 = 1;
		}
	}

	CHECK(has01 == 1);
	CHECK(has23 == 1);
}

static void test_queue_game_state_success_and_failure(void)
{
	ServerState s;
	init_server_state(&s);

	Player dst;
	init_connected_player(&dst, 1);
	dst.registered = 1;

	CHECK(queue_game_state_for_player(&s, &dst) == 0);
	CHECK(player_out_contains(&dst, "STATE_BEGIN\n"));
	CHECK(player_out_contains(&dst, "STATE_END\n"));

	Player full;
	init_connected_player(&full, 2);
	full.outbuf_used = OUTBUF_SIZE - 1;
	CHECK(queue_game_state_for_player(&s, &full) == -1);
}

static void test_maybe_admit_player(void)
{
	ServerState s;
	init_server_state(&s);

	init_connected_player(&s.players[0], 99);
	Player *p = &s.players[0];
	p->registered = 1;
	p->choice_chosen = 1;
	p->spawn_chosen = 1;
	p->choice = 'R';
	p->x = 3;
	p->y = 4;

	CHECK(s.join_deadline == 0);
	maybe_admit_player(&s, 0);

	CHECK(p->admitted == 1);
	CHECK(p->waiting == 1);
	CHECK(s.join_deadline > 0);
	CHECK(player_out_contains(p, "JOINED_MATCH\n"));
}

static void test_handle_command_registration_and_join(void)
{
	ServerState s;
	init_server_state(&s);

	init_connected_player(&s.players[0], 1);
	init_connected_player(&s.players[1], 2);

	handle_command(&s, 0, "HELLO alice");
	CHECK(s.players[0].registered == 1);
	CHECK(player_out_contains(&s.players[0], "WELCOME 1\n"));

	handle_command(&s, 1, "HELLO alice");
	CHECK(player_out_contains(&s.players[1], "ERROR duplicate_name\n"));

	handle_command(&s, 0, "CHOICE R");
	handle_command(&s, 0, "SPAWN 1 1");

	CHECK(s.players[0].choice_chosen == 1);
	CHECK(s.players[0].spawn_chosen == 1);
	CHECK(s.players[0].admitted == 1);
	CHECK(player_out_contains(&s.players[0], "SPAWN_OK 1.0 1.0\n"));
	CHECK(player_out_contains(&s.players[0], "JOINED_MATCH\n"));
}

static void test_handle_command_repick_and_get_state_failure(void)
{
	ServerState s;
	init_server_state(&s);

	init_connected_player(&s.players[0], 7);
	s.players[0].registered = 1;

	handle_command(&s, 0, "REPICK R");
	CHECK(player_out_contains(&s.players[0], "ERROR not_in_repick_phase\n"));

	s.players[0].outbuf_used = OUTBUF_SIZE - 1;
	handle_command(&s, 0, "GET_STATE");
	CHECK(s.players[0].connected == 0);
}

static void test_add_player_capacity(void)
{
	ServerState s;
	init_server_state(&s);

	for (int i = 0; i < MAX_PLAYERS; i++)
	{
		int idx = add_player(&s, INVALID_SOCKET);
		CHECK(idx == i);
	}

	CHECK(add_player(&s, INVALID_SOCKET) == -1);
}

static void test_handle_command_errors_and_quit(void)
{
	ServerState s;
	init_server_state(&s);

	init_connected_player(&s.players[0], 1);

	handle_command(&s, 0, "UNKNOWN");
	CHECK(player_out_contains(&s.players[0], "ERROR bad_command\n"));

	clear_player_output(&s.players[0]);
	handle_command(&s, 0, "CHOICE R");
	CHECK(player_out_contains(&s.players[0], "ERROR register_first\n"));

	handle_command(&s, 0, "HELLO bob");
	clear_player_output(&s.players[0]);

	handle_command(&s, 0, "SPAWN bad input");
	CHECK(player_out_contains(&s.players[0], "ERROR usage_SPAWN_x_y\n"));

	clear_player_output(&s.players[0]);
	handle_command(&s, 0, "SPAWN -1 20");
	CHECK(player_out_contains(&s.players[0], "ERROR bad_spawn\n"));

	clear_player_output(&s.players[0]);
	handle_command(&s, 0, "REMATCH");
	CHECK(player_out_contains(&s.players[0], "ERROR rematch_not_available\n"));

	handle_command(&s, 0, "QUIT");
	CHECK(s.players[0].connected == 0);
}

static void test_spawn_taken_rejection(void)
{
	ServerState s;
	init_server_state(&s);

	init_connected_player(&s.players[0], 1);
	init_connected_player(&s.players[1], 2);

	handle_command(&s, 0, "HELLO a");
	handle_command(&s, 1, "HELLO b");
	handle_command(&s, 0, "CHOICE R");
	handle_command(&s, 0, "SPAWN 2 2");

	clear_player_output(&s.players[1]);
	handle_command(&s, 1, "CHOICE P");
	handle_command(&s, 1, "SPAWN 2 2");
	CHECK(player_out_contains(&s.players[1], "ERROR spawn_taken\n"));
}

static void test_close_lobby_to_active_round(void)
{
	ServerState s;
	init_server_state(&s);

	setup_registered_admitted_player(&s.players[0], 1, "a", 0, 0, 'R');
	setup_registered_admitted_player(&s.players[1], 2, "b", 1, 0, 'P');

	s.phase = PHASE_LOBBY_OPEN;
	s.join_deadline = time(NULL) - 1;

	close_lobby_if_needed(&s);

	CHECK(s.phase == PHASE_ROUND_ACTIVE);
	CHECK(s.round_no == 1);
	CHECK(s.round_deadline > time(NULL));
	CHECK(s.players[0].in_round == 1);
	CHECK(s.players[1].in_round == 1);
	CHECK(player_out_contains(&s.players[0], "ROUND_START"));
	CHECK(player_out_contains(&s.players[1], "ROUND_START"));
}

static void test_close_lobby_not_enough_players(void)
{
	ServerState s;
	init_server_state(&s);

	setup_registered_admitted_player(&s.players[0], 1, "solo", 0, 0, 'R');
	s.join_deadline = time(NULL) - 1;
	s.phase = PHASE_LOBBY_OPEN;

	close_lobby_if_needed(&s);
	CHECK(s.phase == PHASE_GAME_OVER);
	CHECK(player_out_contains(&s.players[0], "GAME_OVER WIN\n"));
}

static void test_expire_unready_setup_players(void)
{
	ServerState s;
	init_server_state(&s);

	init_connected_player(&s.players[0], 1);
	snprintf(s.players[0].name, sizeof(s.players[0].name), "%s", "rdy");
	s.players[0].registered = 1;
	s.players[0].admitted = 1;
	s.players[0].choice_chosen = 1;
	s.players[0].spawn_chosen = 1;
	s.players[0].alive = 1;

	init_connected_player(&s.players[1], 2);
	snprintf(s.players[1].name, sizeof(s.players[1].name), "%s", "slow");
	s.players[1].registered = 1;
	s.players[1].admitted = 1;
	s.players[1].choice_chosen = 1;
	s.players[1].spawn_chosen = 0;
	s.players[1].alive = 1;

	s.phase = PHASE_SETUP;
	s.setup_deadline = time(NULL) - 1;

	expire_unready_setup_players(&s);

	CHECK(s.players[1].connected == 0);
	CHECK(s.phase == PHASE_GAME_OVER);
	CHECK(player_out_contains(&s.players[0], "LEFT slow\n"));
}

static void test_resolve_round_winner_path(void)
{
	ServerState s;
	init_server_state(&s);

	setup_registered_admitted_player(&s.players[0], 1, "a", 0, 0, 'R');
	setup_registered_admitted_player(&s.players[1], 2, "b", 3, 3, 'S');

	s.phase = PHASE_ROUND_ACTIVE;
	resolve_round(&s);

	CHECK(s.players[0].alive == 1);
	CHECK(s.players[1].alive == 0);
	CHECK(s.players[0].x == 3 && s.players[0].y == 3);
	CHECK(s.phase == PHASE_GAME_OVER);
	CHECK(player_out_contains(&s.players[1], "ELIMINATED lost\n"));
	CHECK(player_out_contains(&s.players[0], "GAME_OVER WIN\n"));
	CHECK(player_out_contains(&s.players[1], "GAME_OVER LOSE\n"));
}

static void test_resolve_round_tie_starts_repick(void)
{
	ServerState s;
	init_server_state(&s);

	setup_registered_admitted_player(&s.players[0], 1, "a", 0, 0, 'R');
	setup_registered_admitted_player(&s.players[1], 2, "b", 1, 0, 'R');

	s.phase = PHASE_ROUND_ACTIVE;
	resolve_round(&s);

	CHECK(s.phase == PHASE_REPICK);
	CHECK(s.players[0].repick_submitted == 0);
	CHECK(s.players[1].repick_submitted == 0);
	CHECK(player_out_contains(&s.players[0], "REPICK_START\n"));
	CHECK(player_out_contains(&s.players[1], "REPICK_START\n"));
}

static void test_finish_repicks_moves_to_round(void)
{
	ServerState s;
	init_server_state(&s);

	setup_registered_admitted_player(&s.players[0], 1, "a", 0, 0, 'R');
	setup_registered_admitted_player(&s.players[1], 2, "b", 1, 0, 'R');

	s.phase = PHASE_REPICK;
	s.players[0].repick_submitted = 1;
	s.players[1].repick_submitted = 1;
	s.players[0].repick_choice = 'P';
	s.players[1].repick_choice = 'S';

	finish_repicks(&s);

	CHECK(s.phase == PHASE_ROUND_ACTIVE);
	CHECK(s.round_no == 1);
	CHECK(s.players[0].choice == 'P');
	CHECK(s.players[1].choice == 'S');
	CHECK(s.players[0].repick_submitted == 0);
	CHECK(s.players[1].repick_submitted == 0);
	CHECK(player_out_contains(&s.players[0], "REPICK_DONE\n"));
}

static void test_reevaluate_state_paths(void)
{
	ServerState s;
	init_server_state(&s);

	s.phase = PHASE_LOBBY_OPEN;
	s.join_deadline = time(NULL) + 100;
	reevaluate_state(&s);
	CHECK(s.join_deadline == 0);

	setup_registered_admitted_player(&s.players[0], 1, "a", 0, 0, 'R');
	setup_registered_admitted_player(&s.players[1], 2, "b", 1, 0, 'P');

	s.phase = PHASE_SETUP;
	reevaluate_state(&s);
	CHECK(s.phase == PHASE_ROUND_ACTIVE);

	s.phase = PHASE_REPICK;
	s.players[0].repick_submitted = 1;
	s.players[1].repick_submitted = 1;
	s.players[0].repick_choice = 'S';
	s.players[1].repick_choice = 'R';
	reevaluate_state(&s);
	CHECK(s.phase == PHASE_ROUND_ACTIVE);
}

static void test_reset_match_via_rematch_command(void)
{
	ServerState s;
	init_server_state(&s);

	setup_registered_admitted_player(&s.players[0], 1, "a", 0, 0, 'R');
	setup_registered_admitted_player(&s.players[1], 2, "b", 1, 0, 'P');
	s.phase = PHASE_GAME_OVER;

	handle_command(&s, 0, "REMATCH");

	CHECK(s.phase == PHASE_LOBBY_OPEN);
	CHECK(s.round_no == 0);
	CHECK(s.players[0].admitted == 0);
	CHECK(s.players[1].admitted == 0);
	CHECK(player_out_contains(&s.players[0], "MATCH_RESET\n"));
	CHECK(player_out_contains(&s.players[1], "MATCH_RESET\n"));
}

static void test_can_eat(void)
{
	/* Rock eats Scissors, Scissors eats Paper, Paper eats Rock. */
	CHECK(can_eat('R', 'S') == 1);
	CHECK(can_eat('S', 'P') == 1);
	CHECK(can_eat('P', 'R') == 1);

	/* No eating if same type or non-predator. */
	CHECK(can_eat('R', 'R') == 0);
	CHECK(can_eat('R', 'P') == 0);
	CHECK(can_eat('S', 'R') == 0);
	CHECK(can_eat('S', 'S') == 0);
	CHECK(can_eat('P', 'P') == 0);
	CHECK(can_eat('P', 'S') == 0);
}

static void test_find_nearest_prey(void)
{
	ServerState s;
	init_server_state(&s);

	/* Setup: Rock at (0,0), Paper at (1,0), Scissors at (2,0) */
	setup_registered_admitted_player(&s.players[0], 1, "rock", 0.0f, 0.0f, 'R');
	setup_registered_admitted_player(&s.players[1], 2, "paper", 1.0f, 0.0f, 'P');
	setup_registered_admitted_player(&s.players[2], 3, "scissors", 2.0f, 0.0f, 'S');

	/* Rock should target Scissors (only 2 squares away). */
	int prey_idx = find_nearest_prey(&s, 0);
	CHECK(prey_idx == 2);

	/* Paper should target Rock (only 1 square away). */
	prey_idx = find_nearest_prey(&s, 1);
	CHECK(prey_idx == 0);

	/* Scissors should target Paper (only 1 square away). */
	prey_idx = find_nearest_prey(&s, 2);
	CHECK(prey_idx == 1);
}

static void test_simulate_chase_tick_basic(void)
{
	ServerState s;
	init_server_state(&s);

	/* Setup: Rock at (0,0), Scissors at (3,0) - distance is 3, so rock moves 2 but doesn't capture. */
	setup_registered_admitted_player(&s.players[0], 1, "rock", 0.0f, 0.0f, 'R');
	setup_registered_admitted_player(&s.players[1], 2, "scissors", 3.0f, 0.0f, 'S');

	/* Rock should move toward Scissors. */
	int match_ended = simulate_chase_tick(&s, TEST_CHASE_DT);
	CHECK(match_ended == 0);

	/* Rock should have moved closer (by CHASE_SPEED). */
	CHECK(s.players[0].x > 0.0f);
	CHECK(s.players[0].x < 3.0f);

	/* Scissors should remain at original position (no prey of type it eats). */
	CHECK(s.players[1].x == 3.0f);
	CHECK(s.players[1].y == 0.0f);
}

static void test_simulate_chase_tick_capture(void)
{
	ServerState s;
	init_server_state(&s);

	/* Setup: Rock and Scissors very close, with Paper far away. */
	setup_registered_admitted_player(&s.players[0], 1, "rock", 0.0f, 0.0f, 'R');
	setup_registered_admitted_player(&s.players[1], 2, "scissors", 0.3f, 0.0f, 'S');
	setup_registered_admitted_player(&s.players[2], 3, "paper", 5.0f, 0.0f, 'P');

	/* One tick should result in capture of scissors. */
	int match_ended = simulate_chase_tick(&s, TEST_CHASE_DT);
	CHECK(match_ended == 0);

	/* Scissors should be eliminated. */
	CHECK(s.players[1].alive == 0);
	CHECK(s.players[1].x == -1.0f);
	CHECK(s.players[1].y == -1.0f);

	/* Rock and Paper should still be alive. */
	CHECK(s.players[0].alive == 1);
	CHECK(s.players[2].alive == 1);
}

static void test_simulate_chase_tick_same_type_no_collision(void)
{
	ServerState s;
	init_server_state(&s);

	/* Setup: Two rocks at different positions. */
	setup_registered_admitted_player(&s.players[0], 1, "rock1", 0.0f, 0.0f, 'R');
	setup_registered_admitted_player(&s.players[1], 2, "rock2", 1.0f, 0.0f, 'R');

	float rock1_x = s.players[0].x;
	float rock2_x = s.players[1].x;

	/* Simulate tick: rocks should not interact. */
	simulate_chase_tick(&s, TEST_CHASE_DT);

	/* Both should remain unchanged. */
	CHECK(s.players[0].x == rock1_x);
	CHECK(s.players[0].y == 0.0f);
	CHECK(s.players[1].x == rock2_x);
	CHECK(s.players[1].y == 0.0f);
	CHECK(s.players[0].alive == 1);
	CHECK(s.players[1].alive == 1);
}

static void test_simulate_chase_tick_winner_detection(void)
{
	ServerState s;
	init_server_state(&s);

	/* Setup: Only one Rock alive, Paper already eliminated. */
	setup_registered_admitted_player(&s.players[0], 1, "rock", 0.0f, 0.0f, 'R');
	s.players[0].alive = 1;

	/* No other alive players. */
	for (int i = 1; i < MAX_PLAYERS; i++)
	{
		s.players[i].alive = 0;
	}

	/* Simulate tick should detect only one type remains. */
	int match_ended = simulate_chase_tick(&s, TEST_CHASE_DT);
	CHECK(match_ended == 1);
}

static void test_simulate_chase_tick_multi_player(void)
{
	ServerState s;
	init_server_state(&s);

	/* Setup: Multiple players of different types. */
	setup_registered_admitted_player(&s.players[0], 1, "r1", 0.0f, 0.0f, 'R');
	setup_registered_admitted_player(&s.players[1], 2, "s1", 1.0f, 0.0f, 'S');
	setup_registered_admitted_player(&s.players[2], 3, "p1", 2.0f, 0.0f, 'P');

	/* All should move toward their prey. */
	simulate_chase_tick(&s, TEST_CHASE_DT);

	/* Rock should move right (toward Scissors). */
	CHECK(s.players[0].x > 0.0f);

	/* Scissors should move right (toward Paper). */
	CHECK(s.players[1].x > 1.0f);

	/* Paper should move left (toward Rock). */
	CHECK(s.players[2].x < 2.0f);

	/* All should remain alive (no captures yet). */
	CHECK(s.players[0].alive == 1);
	CHECK(s.players[1].alive == 1);
	CHECK(s.players[2].alive == 1);
}


static void test_multiple_same_type_survivors_all_win(void)
{
	ServerState s;
	init_server_state(&s);

	setup_registered_admitted_player(&s.players[0], 1, "r1", 0, 0, 'R');
	setup_registered_admitted_player(&s.players[1], 2, "r2", 1, 0, 'R');
	setup_registered_admitted_player(&s.players[2], 3, "p1", 5, 0, 'P');

	s.phase = PHASE_ROUND_ACTIVE;
	s.players[2].alive = 0;
	s.players[2].x = -1.0f;
	s.players[2].y = -1.0f;

	reevaluate_state(&s);

	CHECK(s.phase == PHASE_GAME_OVER);
	CHECK(player_out_contains(&s.players[0], "GAME_OVER WIN\n"));
	CHECK(player_out_contains(&s.players[1], "GAME_OVER WIN\n"));
	CHECK(player_out_contains(&s.players[2], "GAME_OVER LOSE\n"));
}

int main(void)
{
	if (net_init() != 0)
	{
		fprintf(stderr, "net_init failed\n");
		return 1;
	}

	test_protocol_queue_and_pop();
	test_protocol_queue_overflow();
	test_game_core_logic();
	test_build_pairs_nearest_neighbors();
	test_queue_game_state_success_and_failure();
	test_maybe_admit_player();
	test_handle_command_registration_and_join();
	test_handle_command_repick_and_get_state_failure();
	test_add_player_capacity();
	test_handle_command_errors_and_quit();
	test_spawn_taken_rejection();
	test_close_lobby_to_active_round();
	test_close_lobby_not_enough_players();
	test_expire_unready_setup_players();
	test_resolve_round_winner_path();
	test_resolve_round_tie_starts_repick();
	test_finish_repicks_moves_to_round();
	test_reevaluate_state_paths();
	test_reset_match_via_rematch_command();
	test_can_eat();
	test_find_nearest_prey();
	test_simulate_chase_tick_basic();
	test_simulate_chase_tick_capture();
	test_simulate_chase_tick_same_type_no_collision();
	test_simulate_chase_tick_winner_detection();
	test_simulate_chase_tick_multi_player();
	test_multiple_same_type_survivors_all_win();

	net_cleanup();

	if (g_tests_failed == 0)
	{
		printf("All tests passed (%d checks).\n", g_tests_run);
		return 0;
	}

	printf("Tests failed: %d of %d checks.\n", g_tests_failed, g_tests_run);
	return 1;
}
