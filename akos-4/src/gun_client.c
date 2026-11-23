/**
 * \file gun_client.c
 * \brief Gun client: connects to the server over TCP.
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

    io_printf("[GUN#%-2d ] подключение к %s:%d...\n", id, host, port);
    int fd = net_connect_retry(host, port, 30000);
    if (fd < 0)
    {
        io_printf("[GUN#%-2d ] сервер не отвечает (30 с)\n", id);
        return 1;
    }

    char role = ROLE_GUN;
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
        io_printf("[GUN#%-2d ] подключение отклонено: %s\n", id, m.note);
        close(fd);
        return 1;
    }
    if (m.phase != PH_WELCOME)
    {
        close(fd);
        return 1;
    }
    io_printf("[GUN#%-2d ] подключено (такт %lld, pid сервера %d)\n",
              id, m.tick, (int)m.pid);

    Rng rng;
    rng_seed(&rng, m.seed ^ (0x85EBCA6Bu * (unsigned int)(id + 1)));

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
            io_printf("[GUN#%-2d ] связь с сервером потеряна\n", id);
            break;
        }
        if (m.phase == PH_KICK)
        {
            io_printf("[GUN#%-2d ] отключён сервером: %s\n", id, m.note);
            break;
        }
        if (m.stop || m.phase == PH_STOP)
        {
            io_printf("[GUN#%-2d ] сервер завершил работу (%s)\n", id, m.note);
            break;
        }

        if (m.phase == PH_OBSERVE)
        {
            Oreshnik g = m.gun;
            gun_decide_observe(&m.field, m.tanks, m.tank_count,
                               g.x, g.y, g.range,
                               &g.has_target, &g.target_id,
                               &g.last_x, &g.last_y,
                               &g.prev_x, &g.prev_y);
            if (g.has_target)
                io_printf("[GUN#%-2d ] наблюдение: цель #%d в (%d,%d)\n",
                          id, g.target_id, g.last_x, g.last_y);
            else
                io_printf("[GUN#%-2d ] наблюдение: целей в радиусе нет\n", id);

            memset(&m, 0, sizeof(m));
            m.phase = PH_OBSERVE;
            m.id = id;
            m.gun = g;
            if (net_write_full(fd, &m, sizeof(m)) != 0)
            {
                io_printf("[GUN#%-2d ] связь с сервером потеряна\n", id);
                break;
            }
        }
        else if (m.phase == PH_FIRE)
        {
            int ax = -1, ay = -1;
            if (m.tick >= m.gun.next_fire)
                gun_decide_aim(m.gun.strategy, m.gun.last_x, m.gun.last_y,
                               m.gun.prev_x, m.gun.prev_y, m.gun.accuracy,
                               m.field.w, m.field.h, &rng, &ax, &ay);
            if (ax >= 0)
                io_printf("[GUN#%-2d ] прицел -> (%d,%d)\n", id, ax, ay);
            else
                io_printf("[GUN#%-2d ] перезарядка (такт %lld < %lld)\n",
                          id, m.tick, m.gun.next_fire);

            memset(&m, 0, sizeof(m));
            m.phase = PH_FIRE;
            m.id = id;
            m.ax = ax;
            m.ay = ay;
            if (net_write_full(fd, &m, sizeof(m)) != 0)
            {
                io_printf("[GUN#%-2d ] связь с сервером потеряна\n", id);
                break;
            }
        }
    }

    close(fd);
    if (sig_stop_requested())
        io_printf("[GUN#%-2d ] завершение по сигналу\n", id);
    return 0;
}
