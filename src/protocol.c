#include "protocol.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

int send_line(socket_t fd, const char *fmt, ...)
{
    char buf[MAX_LINE];
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    if (n < 0 || n >= (int)sizeof(buf) - 2)
    {
        return -1;
    }

    buf[n++] = '\n';

    int sent = 0;
    while (sent < n)
    {
        int rc = send(fd, buf + sent, n - sent, 0);
        if (rc <= 0)
        {
            return -1;
        }
        sent += rc;
    }
    return 0;
}

int queue_line(Player *p, const char *fmt, ...)
{
    char line[MAX_LINE];
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);

    if (n < 0 || n >= (int)sizeof(line) - 2)
    {
        return -1;
    }

    line[n++] = '\n';

    if (p->outbuf_used + (size_t)n > sizeof(p->outbuf))
    {
        return -1;
    }

    memcpy(p->outbuf + p->outbuf_used, line, (size_t)n);
    p->outbuf_used += (size_t)n;
    return 0;
}

int player_has_pending_output(const Player *p)
{
    return p->outbuf_used > 0;
}

int flush_player_output(Player *p)
{
    while (p->outbuf_used > 0)
    {
        int rc = send(p->fd, p->outbuf, (int)p->outbuf_used, 0);

        if (rc > 0)
        {
            size_t sent = (size_t)rc;
            size_t remain = p->outbuf_used - sent;
            memmove(p->outbuf, p->outbuf + sent, remain);
            p->outbuf_used = remain;
            continue;
        }

        if (rc < 0 && NET_WOULD_BLOCK(NET_LAST_ERROR()))
        {
            return 0;
        }

        return -1;
    }

    return 1;
}

int read_into_player_buffer(Player *p)
{
    char tmp[256];
    int rc = recv(p->fd, tmp, sizeof(tmp), 0);

    if (rc == 0)
    {
        return 0;
    }

    if (rc < 0)
    {
        if (NET_WOULD_BLOCK(NET_LAST_ERROR()))
        {
            return 1;
        }
        return -1;
    }

    if (p->inbuf_used + (size_t)rc >= sizeof(p->inbuf))
    {
        return -1;
    }

    memcpy(p->inbuf + p->inbuf_used, tmp, (size_t)rc);
    p->inbuf_used += (size_t)rc;
    return rc;
}

int pop_line(Player *p, char *out, size_t out_sz)
{
    for (size_t i = 0; i < p->inbuf_used; i++)
    {
        if (p->inbuf[i] == '\n')
        {
            size_t len = i;
            if (len > 0 && p->inbuf[len - 1] == '\r')
            {
                len--;
            }
            if (len >= out_sz)
            {
                len = out_sz - 1;
            }

            memcpy(out, p->inbuf, len);
            out[len] = '\0';

            size_t remain = p->inbuf_used - (i + 1);
            memmove(p->inbuf, p->inbuf + i + 1, remain);
            p->inbuf_used = remain;
            return 1;
        }
    }
    return 0;
}