CC=gcc
CFLAGS=-Wall -Wextra -std=c11 -g
PORT?=4242

RAYLIB_BUILD_DIR=../build/_deps/raylib-build/raylib
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

COMMON=protocol.o game.o

ifeq ($(OS),Windows_NT)
SOCKET_LIBS=-lws2_32
CLEAN_CMD=del /Q *.o server.exe client_text.exe client_gui.exe 2>nul
else
CLEAN_CMD=rm -f *.o server client_text client_gui
endif

all: server client_text client_gui

server: server.o $(COMMON)
	$(CC) $(CFLAGS) -DPORT=$(PORT) -o $@ $^ $(SOCKET_LIBS)

client_text: client_text.o protocol.o
	$(CC) $(CFLAGS) -DPORT=$(PORT) -o $@ $^ $(SOCKET_LIBS)

client_gui: client_gui.o protocol.o
	$(CC) $(CFLAGS) $(RAYLIB_CFLAGS) -DPORT=$(PORT) -o $@ $^ $(RAYLIB_LIBS) $(SOCKET_LIBS)

server.o: server.c common.h protocol.h game.h
	$(CC) $(CFLAGS) -c -o $@ $<

client_text.o: client_text.c common.h protocol.h
	$(CC) $(CFLAGS) -c -o $@ $<

client_gui.o: client_gui.c common.h protocol.h
	$(CC) $(CFLAGS) $(RAYLIB_CFLAGS) -c -o $@ $<

protocol.o: protocol.c common.h protocol.h
	$(CC) $(CFLAGS) -c -o $@ $<

game.o: game.c common.h game.h
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	$(CLEAN_CMD)