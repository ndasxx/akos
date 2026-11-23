/**
 * \file tank_client.c
 * \brief Tank client: connects to the server over TCP.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "proto.h"
#include "logic.h"
#include "rng.h"
#include "io.h"
#include "signals.h"
#include "net.h"

static void usage(const char *prog)
{
    fprintf(stderr, "Использование: %s --id N [--host АДРЕС] [--port ПОРТ]\n", prog);
}

int main(int argc, char **argv)
{
    io_init(STDOUT_FILENO);

    int id = 0, port = DEFAULT_PORT;
    const char *host = "127.0.0.1";
    for (int i = 1; i < argc; ++i)
        if (!strcmp(argv[i], "--help"))
        {
            usage(argv[0]);
            return 0;
        }
    for (int i = 1; i + 1 < argc; ++i)
    {
        if (!strcmp(argv[i], "--id"))
            id = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--host"))
            host = argv[i + 1];
        else if (!strcmp(argv[i], "--port"))
            port = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--help"))
        {
            usage(argv[0]);
            return 0;
        }
    }
    if (id < 1)
    {
        usage(argv[0]);
        return 1;
    }
    if (sig_install() != 0)
    {
        fprintf(stderr, "Не удалось установить обработчики сигналов\n");
        return 1;
    }

    io_printf("[TANK#%-2d] подключение к %s:%d...\n", id, host, port);
    int fd = net_connect_retry(host, port, 30000);
    if (fd < 0)
    {
        io_printf("[TANK#%-2d] сервер не отвечает (30 с)\n", id);
        return 1;
    }

    char role = ROLE_TANK;
    if (net_write_full(fd, &role, 1) != 0)
        return 1;
    Msg m;
    memset(&m, 0, sizeof(m));
    m.phase = PH_ATTACH;
    m.id = id;
    m.pid = getpid();
    if (net_write_full(fd, &m, sizeof(m)) != 0)
        return 1;
    if (net_read_full(fd, &m, sizeof(m)) != 0)
        return 1;
    if (m.phase == PH_KICK)
    {
        io_printf("[TANK#%-2d] подключение отклонено: %s\n", id, m.note);
        close(fd);
        return 1;
    }
    if (m.phase != PH_WELCOME)
    {
        close(fd);
        return 1;
    }
    io_printf("[TANK#%-2d] подключено (такт %lld, pid сервера %d)\n",
              id, m.tick, (int)m.pid);

    Rng rng;
    rng_seed(&rng, m.seed ^ (0x9E3779B9u * (unsigned int)(id + 1)));

    for (;;)
    {
        int w = net_wait_readable(fd, 1000);
        if (w == 0)
        {
            if (sig_stop_requested())
                break;
            continue;
        }
        if (w < 0 || net_read_full(fd, &m, sizeof(m)) != 0)
        {
            io_printf("[TANK#%-2d] связь с сервером потеряна\n", id);
            break;
        }
        if (m.phase == PH_KICK)
        {
            io_printf("[TANK#%-2d] отключён сервером: %s\n", id, m.note);
            break;
        }
        if (m.stop || m.phase == PH_STOP)
        {
            io_printf("[TANK#%-2d] сервер завершил работу (%s)\n", id, m.note);
            break;
        }
        if (m.phase != PH_MOVE)
            continue;

        Tank self = m.tanks[id - 1];
        int dx, dy;
        tank_decide_step(&m.field, m.tanks, m.tank_count, &self, &rng, &dx, &dy);
        if (dx == 0 && dy == 0)
            io_printf("[TANK#%-2d] решение: остаюсь на (%d,%d)\n",
                      id, self.x, self.y);
        else
            io_printf("[TANK#%-2d] решение: шаг (%d,%d) -> (%d,%d)\n",
                      id, self.x, self.y, self.x + dx, self.y + dy);

        memset(&m, 0, sizeof(m));
        m.phase = PH_MOVE;
        m.id = id;
        m.ax = dx;
        m.ay = dy;
        if (net_write_full(fd, &m, sizeof(m)) != 0)
        {
            io_printf("[TANK#%-2d] связь с сервером потеряна\n", id);
            break;
        }
    }

    close(fd);
    if (sig_stop_requested())
        io_printf("[TANK#%-2d] завершение по сигналу\n", id);
    return 0;
}
