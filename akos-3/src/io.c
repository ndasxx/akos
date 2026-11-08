/**
 * \file io.c
 * \brief Implementation of the output layer over write
 */

#include "io.h"
#include <unistd.h>
#include <stdarg.h>
#include <stdio.h>
#include <errno.h>

/* Main output descriptor (default: stdout) */
static int g_fd = 1;

/* Mirror descriptor for logging (-1 = disabled) */
static int g_mirror = -1;

/* Optional cross-process line lock (may be NULL) */
static sem_t *g_lock = NULL;

int io_init(int fd)
{
    g_fd = fd;
    return 0;
}

void io_set_lock(sem_t *lock)
{
    g_lock = lock;
}

void io_set_mirror(int fd)
{
    g_mirror = fd;
}

/* Write exactly n bytes handling partial writes. */
static void write_all(int fd, const char *buf, size_t n)
{
    size_t off = 0;
    while (off < n)
    {
        ssize_t w = write(fd, buf + off, n - off);
        if (w <= 0)
            break;
        off += (size_t)w;
    }
}

/* Retry wait if interrupted by a signal. */
static void lock_wait(sem_t *s)
{
    while (sem_wait(s) == -1 && errno == EINTR)
        ;
}

void io_printf(const char *fmt, ...)
{
    char buf[1024];
    va_list ap;

    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (n <= 0)
        return;
    if (n > (int)sizeof(buf))
        n = (int)sizeof(buf);

    if (g_lock)
        lock_wait(g_lock);
    write_all(g_fd, buf, (size_t)n);
    if (g_mirror >= 0)
        write_all(g_mirror, buf, (size_t)n);
    if (g_lock)
        sem_post(g_lock);
}
