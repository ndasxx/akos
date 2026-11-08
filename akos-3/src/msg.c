/**
 * \file msg.c
 * \brief Implementation of the FIFO message helpers.
 */

#include "msg.h"
#include <stdio.h>
#include <unistd.h>
#include <errno.h>

void fifo_path(char *buf, size_t bufsz, const char *kind, const char *role, int id)
{
    snprintf(buf, bufsz, "/tmp/akos3d_%s_%s_%d", kind, role, id);
}

int write_full(int fd, const void *buf, size_t n)
{
    const char *p = (const char *)buf;
    size_t off = 0;
    while (off < n)
    {
        ssize_t w = write(fd, p + off, n - off);
        if (w <= 0)
        {
            if (w < 0 && errno == EINTR)
                continue;
            return -1;
        }
        off += (size_t)w;
    }
    return 0;
}

int read_full(int fd, void *buf, size_t n)
{
    char *p = (char *)buf;
    size_t off = 0;
    while (off < n)
    {
        ssize_t r = read(fd, p + off, n - off);
        if (r < 0)
        {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (r == 0)
            return -1;
        off += (size_t)r;
    }
    return 0;
}
