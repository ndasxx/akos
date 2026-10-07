/**
 * \file log.c
 * \brief Implementation of file-based logging.
 */

#include "log.h"
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include "common.h"

/* Current log descriptor. -1 means the log is not open. */
static int g_fd = -1;

int log_open(const char *path)
{
    if (!path)
        return -1;
    g_fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    return g_fd;
}

void log_close(void)
{
    if (g_fd >= 0)
    {
        /* Flush pending data to disk before closing */
        fsync(g_fd);
        close(g_fd);
        g_fd = -1;
    }
}

int log_fd(void)
{
    return g_fd;
}

/*
 * Write the entire string to the log, handling partial writes.
 * No newline is appended — the caller controls line formatting.
 */
void log_line(const char *line)
{
    if (g_fd < 0 || !line)
        return;

    size_t n = strlen(line);
    size_t off = 0;
    while (off < n)
    {
        ssize_t w = write(g_fd, line + off, n - off);
        if (w <= 0)
            break;
        off += (size_t)w;
    }
}