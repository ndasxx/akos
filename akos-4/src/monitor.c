/**
 * \file monitor.c
 * \brief Monitor client: the integrated view of the simulation.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "proto.h"
#include "io.h"
#include "signals.h"
#include "field.h"
#include "net.h"

static void print_snapshot(const Msg *m)
{
    io_printf("\n[МОНИТОР] ===== такт %lld =====\n", m->tick);

    char buf[4096];
    field_print(&m->field, buf, sizeof(buf));
    io_printf("%s", buf);

    io_printf("Танки:\n");
    for (int i = 0; i < m->tank_count; ++i)
    {
        const Tank *t = &m->tanks[i];
        io_printf("  #%d %-11s hp %d/%d  позиция (%d,%d)  шагов %lld\n",
                  t->id, t->alive ? "жив" : "УНИЧТОЖЕН",
                  t->hp, t->max_hp, t->x, t->y, t->moved_cells);
    }

    io_printf("Орудия:\n");
    for (int i = 0; i < m->gun_count; ++i)
    {
        const Oreshnik *g = &m->guns[i];
        io_printf("  #%d патронов %2d  выстрелов %lld  цель: %s",
                  g->id, g->ammo, g->shots_fired,
                  g->has_target ? "есть" : "нет");
        if (g->has_target)
            io_printf(" (последняя позиция %d,%d)", g->last_x, g->last_y);
        io_printf("\n");
    }

    int flying = 0;
    for (int i = 0; i < MAX_PROJECTILES; ++i)
        flying += m->projectiles[i].alive != 0;
    io_printf("Снарядов в полёте: %d\n", flying);
    io_printf("Статистика: выстрелов %lld, попаданий %lld, промахов %lld, "
              "танков уничтожено %lld\n",
              m->stats.shots_total, m->stats.shots_hit,
              m->stats.shots_missed, m->stats.tanks_destroyed);
}

int main(int argc, char **argv)
{
    io_init(STDOUT_FILENO);

    int port = DEFAULT_PORT;
    const char *host = "127.0.0.1";
    for (int i = 1; i < argc; ++i)
        if (!strcmp(argv[i], "--help"))
        {
            fprintf(stderr, "Использование: %s [--host АДРЕС] [--port ПОРТ]\n",
                    argv[0]);
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
            fprintf(stderr, "Использование: %s [--host АДРЕС] [--port ПОРТ]\n",
                    argv[0]);
            return 0;
        }
    }
    if (sig_install() != 0)
    {
        fprintf(stderr, "Не удалось установить обработчики сигналов\n");
        return 1;
    }

    io_printf("[МОНИТОР] подключение к %s:%d...\n", host, port);
    int fd = net_connect_retry(host, port, 30000);
    if (fd < 0)
    {
        io_printf("[МОНИТОР] сервер не отвечает (30 с)\n");
        return 1;
    }

    char role = ROLE_MONITOR;
    if (net_write_full(fd, &role, 1) != 0)
        return 1;

    for (;;)
    {
        int w = net_wait_readable(fd, 1000);
        if (w == 0)
        {
            if (sig_stop_requested())
                break;
            continue;
        }
        Msg m;
        if (w < 0 || net_read_full(fd, &m, sizeof(m)) != 0)
        {
            io_printf("[МОНИТОР] связь с сервером потеряна\n");
            break;
        }
        if (m.phase == PH_STOP || m.stop)
        {
            print_snapshot(&m);
            io_printf("[МОНИТОР] сервер завершил работу (%s)\n", m.note);
            break;
        }
        if (m.phase == PH_MON)
            print_snapshot(&m);
    }

    close(fd);
    return 0;
}
