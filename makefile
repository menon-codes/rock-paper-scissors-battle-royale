CC=gcc
CFLAGS=-Wall -Wextra -std=c11 -g
PORT?=4242

SRC_DIR=src
TEST_DIR=tests
BUILD_DIR=build
OBJ_DIR=$(BUILD_DIR)/obj
BIN_DIR=$(BUILD_DIR)/bin
TEST_BIN_DIR=$(BUILD_DIR)/tests

CPPFLAGS=-I$(SRC_DIR)

SOCKET_LIBS=
MATH_LIBS=-lm
NCURSESW_CFLAGS=-D_DARWIN_C_SOURCE
NCURSESW_LIBS=-lncurses
MKDIR_BIN=mkdir -p $(BIN_DIR)
MKDIR_OBJ=mkdir -p $(OBJ_DIR)
MKDIR_TEST_BIN=mkdir -p $(TEST_BIN_DIR)

SERVER_OBJ=$(OBJ_DIR)/server.o
SERVER_STATE_OBJ=$(OBJ_DIR)/server_state.o
SERVER_COMMANDS_OBJ=$(OBJ_DIR)/server_commands.o
CLIENT_TEXT_OBJ=$(OBJ_DIR)/client_text.o
CLIENT_NETWORK_OBJ=$(OBJ_DIR)/client_network.o
CLIENT_TEXT_MESSAGES_OBJ=$(OBJ_DIR)/client_text_messages.o
PROTOCOL_OBJ=$(OBJ_DIR)/protocol.o
GAME_OBJ=$(OBJ_DIR)/game.o
CHASE_SIMULATION_OBJ=$(OBJ_DIR)/chase_simulation.o
TEST_RUNNER_OBJ=$(OBJ_DIR)/test_runner.o
INTEGRATION_RUNNER_OBJ=$(OBJ_DIR)/integration_runner.o

SERVER_BIN=$(BIN_DIR)/server
CLIENT_TEXT_BIN=$(BIN_DIR)/client_text
TEST_BIN=$(TEST_BIN_DIR)/unit_tests
INTEGRATION_TEST_BIN=$(TEST_BIN_DIR)/integration_tests

all: $(SERVER_BIN) $(CLIENT_TEXT_BIN)

cli: $(SERVER_BIN) $(CLIENT_TEXT_BIN)

test: $(TEST_BIN)
	$(TEST_BIN)

integration-test: $(SERVER_BIN) $(INTEGRATION_TEST_BIN)
	$(INTEGRATION_TEST_BIN)

test-all: test integration-test

$(SERVER_BIN): $(SERVER_OBJ) $(SERVER_STATE_OBJ) $(SERVER_COMMANDS_OBJ) $(PROTOCOL_OBJ) $(GAME_OBJ) $(CHASE_SIMULATION_OBJ) | dirs
	$(CC) $(CFLAGS) -DPORT=$(PORT) -o $@ $^ $(MATH_LIBS) $(SOCKET_LIBS)

$(CLIENT_TEXT_BIN): $(CLIENT_TEXT_OBJ) $(CLIENT_NETWORK_OBJ) $(CLIENT_TEXT_MESSAGES_OBJ) $(PROTOCOL_OBJ) | dirs
	$(CC) $(CFLAGS) $(NCURSESW_CFLAGS) -DPORT=$(PORT) -o $@ $^ $(NCURSESW_LIBS) $(SOCKET_LIBS)

$(TEST_BIN): $(TEST_RUNNER_OBJ) $(SERVER_STATE_OBJ) $(SERVER_COMMANDS_OBJ) $(PROTOCOL_OBJ) $(GAME_OBJ) $(CHASE_SIMULATION_OBJ) | dirs
	$(CC) $(CFLAGS) -DPORT=$(PORT) -o $@ $^ $(MATH_LIBS) $(SOCKET_LIBS)

$(INTEGRATION_TEST_BIN): $(INTEGRATION_RUNNER_OBJ) $(PROTOCOL_OBJ) | dirs
	$(CC) $(CFLAGS) -DPORT=$(PORT) -o $@ $^ $(SOCKET_LIBS)

$(SERVER_OBJ): $(SRC_DIR)/server.c $(SRC_DIR)/common.h $(SRC_DIR)/protocol.h $(SRC_DIR)/game.h $(SRC_DIR)/chase_simulation.h $(SRC_DIR)/server_commands.h $(SRC_DIR)/server_state.h | dirs
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

$(SERVER_STATE_OBJ): $(SRC_DIR)/server_state.c $(SRC_DIR)/common.h $(SRC_DIR)/server_state.h $(SRC_DIR)/protocol.h $(SRC_DIR)/game.h | dirs
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

$(SERVER_COMMANDS_OBJ): $(SRC_DIR)/server_commands.c $(SRC_DIR)/common.h $(SRC_DIR)/server_commands.h $(SRC_DIR)/server_state.h $(SRC_DIR)/protocol.h $(SRC_DIR)/game.h | dirs
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

$(CLIENT_TEXT_OBJ): $(SRC_DIR)/client_text.c $(SRC_DIR)/common.h $(SRC_DIR)/protocol.h $(SRC_DIR)/client_state.h | dirs
	$(CC) $(CFLAGS) $(CPPFLAGS) $(NCURSESW_CFLAGS) -c -o $@ $<

$(CLIENT_NETWORK_OBJ): $(SRC_DIR)/client_network.c $(SRC_DIR)/common.h $(SRC_DIR)/client_state.h $(SRC_DIR)/protocol.h | dirs
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

$(CLIENT_TEXT_MESSAGES_OBJ): $(SRC_DIR)/client_text_messages.c $(SRC_DIR)/common.h $(SRC_DIR)/client_state.h | dirs
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

$(PROTOCOL_OBJ): $(SRC_DIR)/protocol.c $(SRC_DIR)/common.h $(SRC_DIR)/protocol.h | dirs
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

$(GAME_OBJ): $(SRC_DIR)/game.c $(SRC_DIR)/common.h $(SRC_DIR)/game.h | dirs
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

$(CHASE_SIMULATION_OBJ): $(SRC_DIR)/chase_simulation.c $(SRC_DIR)/common.h $(SRC_DIR)/chase_simulation.h $(SRC_DIR)/game.h | dirs
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

$(TEST_RUNNER_OBJ): $(TEST_DIR)/test_runner.c $(SRC_DIR)/common.h $(SRC_DIR)/game.h $(SRC_DIR)/protocol.h $(SRC_DIR)/server_state.h $(SRC_DIR)/server_commands.h $(SRC_DIR)/chase_simulation.h | dirs
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

$(INTEGRATION_RUNNER_OBJ): $(TEST_DIR)/integration_runner.c $(SRC_DIR)/common.h $(SRC_DIR)/protocol.h | dirs
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

dirs:
	$(MKDIR_BIN)
	$(MKDIR_OBJ)
	$(MKDIR_TEST_BIN)

clean:
	rm -f $(OBJ_DIR)/*.o $(SERVER_BIN) $(CLIENT_TEXT_BIN) $(TEST_BIN) $(INTEGRATION_TEST_BIN)

.PHONY: all cli test integration-test test-all dirs clean