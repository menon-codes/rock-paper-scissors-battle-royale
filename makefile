CC=gcc
CFLAGS=-Wall -Wextra -std=c11 -g
PORT?=4242

SRC_DIR=src
TEST_DIR=tests
BUILD_DIR=build
OBJ_DIR=$(BUILD_DIR)/obj
BIN_DIR=$(BUILD_DIR)/bin
TEST_BIN_DIR=$(BUILD_DIR)/tests

RAYLIB_BUILD_DIR=$(BUILD_DIR)/_deps/raylib-build/raylib
RAYLIB_LOCAL_HEADER=$(RAYLIB_BUILD_DIR)/include/raylib.h
RAYLIB_LOCAL_LIB=$(RAYLIB_BUILD_DIR)/libraylib.a

ifeq ($(wildcard $(RAYLIB_LOCAL_HEADER)),)
RAYLIB_CFLAGS := $(shell pkg-config --cflags raylib)
RAYLIB_LIBS := $(shell pkg-config --libs raylib)
else
RAYLIB_CFLAGS := -I$(RAYLIB_BUILD_DIR)/include
ifeq ($(OS),Windows_NT)
RAYLIB_LIBS := $(RAYLIB_LOCAL_LIB) -lopengl32 -lgdi32 -lwinmm
else
RAYLIB_LIBS := $(RAYLIB_LOCAL_LIB) -lm -ldl -lpthread
endif
endif

CPPFLAGS=-I$(SRC_DIR)

ifeq ($(OS),Windows_NT)
SOCKET_LIBS=-lws2_32
NCURSESW_CFLAGS=
NCURSESW_LIBS=
MKDIR_BIN=if not exist "$(subst /,\,$(BIN_DIR))" mkdir "$(subst /,\,$(BIN_DIR))"
MKDIR_OBJ=if not exist "$(subst /,\,$(OBJ_DIR))" mkdir "$(subst /,\,$(OBJ_DIR))"
MKDIR_TEST_BIN=if not exist "$(subst /,\,$(TEST_BIN_DIR))" mkdir "$(subst /,\,$(TEST_BIN_DIR))"
else
SOCKET_LIBS=
NCURSESW_CFLAGS=$(shell pkg-config --cflags ncursesw 2>/dev/null)
NCURSESW_LIBS=$(shell pkg-config --libs ncursesw 2>/dev/null || echo -lncursesw -ltinfo)
MKDIR_BIN=mkdir -p $(BIN_DIR)
MKDIR_OBJ=mkdir -p $(OBJ_DIR)
MKDIR_TEST_BIN=mkdir -p $(TEST_BIN_DIR)
endif

SERVER_OBJ=$(OBJ_DIR)/server.o
SERVER_STATE_OBJ=$(OBJ_DIR)/server_state.o
SERVER_COMMANDS_OBJ=$(OBJ_DIR)/server_commands.o
CLIENT_TEXT_OBJ=$(OBJ_DIR)/client_text.o
CLIENT_GUI_OBJ=$(OBJ_DIR)/client_gui.o
CLIENT_GUI_NETWORK_OBJ=$(OBJ_DIR)/client_gui_network.o
CLIENT_GUI_MESSAGES_OBJ=$(OBJ_DIR)/client_gui_messages.o
PROTOCOL_OBJ=$(OBJ_DIR)/protocol.o
GAME_OBJ=$(OBJ_DIR)/game.o
CHASE_SIMULATION_OBJ=$(OBJ_DIR)/chase_simulation.o
TEST_RUNNER_OBJ=$(OBJ_DIR)/test_runner.o
INTEGRATION_RUNNER_OBJ=$(OBJ_DIR)/integration_runner.o

SERVER_BIN=$(BIN_DIR)/server
CLIENT_TEXT_BIN=$(BIN_DIR)/client_text
CLIENT_GUI_BIN=$(BIN_DIR)/client_gui
TEST_BIN=$(TEST_BIN_DIR)/unit_tests
INTEGRATION_TEST_BIN=$(TEST_BIN_DIR)/integration_tests

all: $(SERVER_BIN) $(CLIENT_TEXT_BIN) $(CLIENT_GUI_BIN)

test: $(TEST_BIN)
	$(TEST_BIN)

integration-test: $(SERVER_BIN) $(INTEGRATION_TEST_BIN)
	$(INTEGRATION_TEST_BIN)

test-all: test integration-test

$(SERVER_BIN): $(SERVER_OBJ) $(SERVER_STATE_OBJ) $(SERVER_COMMANDS_OBJ) $(PROTOCOL_OBJ) $(GAME_OBJ) $(CHASE_SIMULATION_OBJ) | dirs
	$(CC) $(CFLAGS) -DPORT=$(PORT) -o $@ $^ $(SOCKET_LIBS)

$(CLIENT_TEXT_BIN): $(CLIENT_TEXT_OBJ) $(CLIENT_GUI_NETWORK_OBJ) $(CLIENT_GUI_MESSAGES_OBJ) $(PROTOCOL_OBJ) | dirs
	$(CC) $(CFLAGS) $(NCURSESW_CFLAGS) -DPORT=$(PORT) -o $@ $^ $(NCURSESW_LIBS) $(SOCKET_LIBS)

$(CLIENT_GUI_BIN): $(CLIENT_GUI_OBJ) $(CLIENT_GUI_NETWORK_OBJ) $(CLIENT_GUI_MESSAGES_OBJ) $(PROTOCOL_OBJ) | dirs
	$(CC) $(CFLAGS) $(RAYLIB_CFLAGS) -DPORT=$(PORT) -o $@ $^ $(RAYLIB_LIBS) $(SOCKET_LIBS)

$(TEST_BIN): $(TEST_RUNNER_OBJ) $(SERVER_STATE_OBJ) $(SERVER_COMMANDS_OBJ) $(PROTOCOL_OBJ) $(GAME_OBJ) $(CHASE_SIMULATION_OBJ) | dirs
	$(CC) $(CFLAGS) -DPORT=$(PORT) -o $@ $^ $(SOCKET_LIBS)

$(INTEGRATION_TEST_BIN): $(INTEGRATION_RUNNER_OBJ) $(PROTOCOL_OBJ) | dirs
	$(CC) $(CFLAGS) -DPORT=$(PORT) -o $@ $^ $(SOCKET_LIBS)

$(SERVER_OBJ): $(SRC_DIR)/server.c $(SRC_DIR)/common.h $(SRC_DIR)/protocol.h $(SRC_DIR)/game.h $(SRC_DIR)/chase_simulation.h | dirs
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

$(SERVER_STATE_OBJ): $(SRC_DIR)/server_state.c $(SRC_DIR)/common.h $(SRC_DIR)/server_state.h $(SRC_DIR)/protocol.h $(SRC_DIR)/game.h | dirs
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

$(SERVER_COMMANDS_OBJ): $(SRC_DIR)/server_commands.c $(SRC_DIR)/common.h $(SRC_DIR)/server_commands.h $(SRC_DIR)/server_state.h $(SRC_DIR)/protocol.h $(SRC_DIR)/game.h | dirs
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

$(CLIENT_TEXT_OBJ): $(SRC_DIR)/client_text.c $(SRC_DIR)/common.h $(SRC_DIR)/protocol.h $(SRC_DIR)/client_gui_state.h | dirs
	$(CC) $(CFLAGS) $(CPPFLAGS) $(NCURSESW_CFLAGS) -c -o $@ $<

$(CLIENT_GUI_OBJ): $(SRC_DIR)/client_gui.c $(SRC_DIR)/common.h $(SRC_DIR)/protocol.h | dirs
	$(CC) $(CFLAGS) $(CPPFLAGS) $(RAYLIB_CFLAGS) -c -o $@ $<

$(CLIENT_GUI_NETWORK_OBJ): $(SRC_DIR)/client_gui_network.c $(SRC_DIR)/common.h $(SRC_DIR)/client_gui_state.h $(SRC_DIR)/protocol.h | dirs
	$(CC) $(CFLAGS) $(CPPFLAGS) $(RAYLIB_CFLAGS) -c -o $@ $<

$(CLIENT_GUI_MESSAGES_OBJ): $(SRC_DIR)/client_gui_messages.c $(SRC_DIR)/common.h $(SRC_DIR)/client_gui_state.h | dirs
	$(CC) $(CFLAGS) $(CPPFLAGS) $(RAYLIB_CFLAGS) -c -o $@ $<

$(PROTOCOL_OBJ): $(SRC_DIR)/protocol.c $(SRC_DIR)/common.h $(SRC_DIR)/protocol.h | dirs
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

$(GAME_OBJ): $(SRC_DIR)/game.c $(SRC_DIR)/common.h $(SRC_DIR)/game.h | dirs
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

$(CHASE_SIMULATION_OBJ): $(SRC_DIR)/chase_simulation.c $(SRC_DIR)/common.h $(SRC_DIR)/chase_simulation.h | dirs
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

$(TEST_RUNNER_OBJ): $(TEST_DIR)/test_runner.c $(SRC_DIR)/common.h $(SRC_DIR)/game.h $(SRC_DIR)/protocol.h $(SRC_DIR)/server_state.h $(SRC_DIR)/server_commands.h | dirs
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

$(INTEGRATION_RUNNER_OBJ): $(TEST_DIR)/integration_runner.c $(SRC_DIR)/common.h $(SRC_DIR)/protocol.h | dirs
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

dirs:
	$(MKDIR_BIN)
	$(MKDIR_OBJ)
	$(MKDIR_TEST_BIN)

clean:
ifeq ($(OS),Windows_NT)
	-if exist "$(subst /,\,$(OBJ_DIR))" del /Q "$(subst /,\,$(OBJ_DIR))\*.o" 2>nul
	-if exist "$(subst /,\,$(BIN_DIR))" del /Q "$(subst /,\,$(BIN_DIR))\server.exe" "$(subst /,\,$(BIN_DIR))\client_text.exe" "$(subst /,\,$(BIN_DIR))\client_gui.exe" 2>nul
	-if exist "$(subst /,\,$(BIN_DIR))" del /Q "$(subst /,\,$(BIN_DIR))\server" "$(subst /,\,$(BIN_DIR))\client_text" "$(subst /,\,$(BIN_DIR))\client_gui" 2>nul
	-if exist "$(subst /,\,$(TEST_BIN_DIR))" del /Q "$(subst /,\,$(TEST_BIN_DIR))\unit_tests" "$(subst /,\,$(TEST_BIN_DIR))\unit_tests.exe" "$(subst /,\,$(TEST_BIN_DIR))\integration_tests" "$(subst /,\,$(TEST_BIN_DIR))\integration_tests.exe" 2>nul
else
	rm -f $(OBJ_DIR)/*.o $(SERVER_BIN) $(CLIENT_TEXT_BIN) $(CLIENT_GUI_BIN) $(TEST_BIN) $(INTEGRATION_TEST_BIN)
endif
