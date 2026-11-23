/**
 * \file control.c
 * \brief Operator console: manages the clients of the server.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>

#include "proto.h"
#include "io.h"
#include "signals.h"
#include "net.h"

static void usage(const char *prog)
{
    fprintf(stderr, "Использование: %s [--host АДРЕС] [--port ПОРТ]\n", prog);
}

/* Prints everything the server has sent (one reply per command). */
static int drain_replies_timeout(int fd, int ms)
{
    char buf[1024];
    for (;;)
    {
        int w = net_wait_readable(fd, ms);
        if (w <= 0)
            return w;
        ssize_t r = recv(fd, buf, sizeof(buf) - 1, 0);
        if (r <= 0)
            return -1;
        buf[r] = 0;
        io_printf("%s", buf);
        if (strchr(buf, '\n'))
            return 1;
    }
}

int main(int argc, char **argv)
{
    io_init(STDOUT_FILENO);

    int port = DEFAULT_PORT;
    const char *host = "127.0.0.1";
    for (int i = 1; i < argc; ++i)
        if (!strcmp(argv[i], "--help"))
        {
            usage(argv[0]);
            return 0;
        }
    for (int i = 1; i + 1 < argc; ++i)
    {
        if (!strcmp(argv[i], "--host"))
            host = argv[i + 1];
        else if (!strcmp(argv[i], "--port"))
            port = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--help"))
        {
            usage(argv[0]);
            return 0;
        }
    }
    if (sig_install() != 0)
    {
        fprintf(stderr, "Не удалось установить обработчики сигналов\n");
        return 1;
    }

    io_printf("[КОНТРОЛЬ] подключение к %s:%d...\n", host, port);
    int fd = net_connect_retry(host, port, 30000);
    if (fd < 0)
    {
        io_printf("[КОНТРОЛЬ] сервер не отвечает (30 с)\n");
        return 1;
    }

    char role = ROLE_CONTROL;
    if (net_write_full(fd, &role, 1) != 0)
        return 1;
    if (drain_replies_timeout(fd, 1000) < 0)
    {
        io_printf("[КОНТРОЛЬ] связь с сервером потеряна\n");
        close(fd);
        return 1;
    }

    io_printf("введите команду (help — справка, Ctrl+D — выход)\n");

    char line[256];
    size_t len = 0;
    for (;;)
    {
        char c;
        ssize_t r = read(STDIN_FILENO, &c, 1);
        if (r <= 0 || sig_stop_requested())
            break;
        if (c != '\n')
        {
            if (len < sizeof(line) - 1)
                line[len++] = c;
            continue;
        }
        line[len] = 0;
        len = 0;
        if (!line[0])
            continue;

        if (!strcmp(line, "quit") || !strcmp(line, "exit"))
            break;
        size_t l = strlen(line);
        line[l] = '\n';
        if (net_write_full(fd, line, l + 1) != 0)
        {
            io_printf("[КОНТРОЛЬ] связь с сервером потеряна\n");
            break;
        }
        if (drain_replies_timeout(fd, 1000) < 0)
        {
            io_printf("[КОНТРОЛЬ] связь с сервером потеряна\n");
            break;
        }
        if (!strcmp(line, "stop\n"))
            break;
    }

    drain_replies_timeout(fd, 1000);

    close(fd);
    io_printf("[КОНТРОЛЬ] завершение\n");
    return 0;
}
