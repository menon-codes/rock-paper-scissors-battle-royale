CC=gcc
CFLAGS=-Wall -Wextra -std=c11 -g
PORT?=4242

RAYLIB_CFLAGS := $(shell pkg-config --cflags raylib)
RAYLIB_LIBS := $(shell pkg-config --libs raylib)

COMMON=protocol.o game.o

all: server client_text client_gui

server: server.o $(COMMON)
	$(CC) $(CFLAGS) -DPORT=$(PORT) -o $@ $^

client_text: client_text.o protocol.o
	$(CC) $(CFLAGS) -DPORT=$(PORT) -o $@ $^

client_gui: client_gui.o protocol.o
	$(CC) $(CFLAGS) $(RAYLIB_CFLAGS) -DPORT=$(PORT) -o $@ $^ $(RAYLIB_LIBS)

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
	rm -f *.o server client_text client_gui