#ifndef PROTOCOL_H
#define PROTOCOL_H

#include "common.h"

int send_line(int fd, const char *fmt, ...);
int queue_line(Player *p, const char *fmt, ...);
int player_has_pending_output(const Player *p);
int flush_player_output(Player *p);

int read_into_player_buffer(Player *p);
int pop_line(Player *p, char *out, size_t out_sz);

#endif