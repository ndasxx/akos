/**
 * \file io.c
 * \brief Implementation of the output layer over write(2).
 */

#include "io.h"
#include <unistd.h>
#include <stdarg.h>
#include <stdio.h>

/* Main output descriptor (default: stdout) */
static int g_fd = 1;

/* Mirror descriptor for logging (-1 = disabled) */
static int g_mirror = -1;

int io_init(int fd)
{
    g_fd = fd;
    return 0;
}

void io_set_mirror(int fd)
{
    g_mirror = fd;
}

/*
 * Write exactly n bytes to the descriptor, handling partial writes.
 * The system call write() may return fewer bytes than requested, so
 * we loop until all data is sent or an error occurs.
 */
static void write_all(int fd, const char *buf, size_t n)
{
    size_t off = 0;
    while (off < n)
    {
        ssize_t w = write(fd, buf + off, n - off);
        if (w <= 0)
            break; /* Error or EOF */
        off += (size_t)w;
    }
}

/*
 * Format the string into a fixed-size buffer and write it to both
 * the main descriptor and the mirror. If the formatted string exceeds
 * the buffer size (1024 bytes), it is truncated.
 */
void io_printf(const char *fmt, ...)
{
    char buf[1024];
    va_list ap;

    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (n <= 0)
        return; /* Formatting error */
    if (n > (int)sizeof(buf))
        n = (int)sizeof(buf); /* Truncate */

    write_all(g_fd, buf, (size_t)n);
    if (g_mirror >= 0)
        write_all(g_mirror, buf, (size_t)n);
}