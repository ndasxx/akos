/**
 * \file main.c
 * \brief Entry point for the tank simulation.
 *
 * Initializes I/O, signal handlers, simulation context, loads configuration,
 * runs the simulation, prints final statistics, and cleans up resources.
 */

#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "sim.h"
#include "config.h"
#include "io.h"
#include "log.h"
#include "signals.h"

/* Find the value following a given key in argv. Returns NULL if not found. */
static const char *find_arg(int argc, char **argv, const char *key)
{
    for (int i = 1; i + 1 < argc; ++i)
        if (!strcmp(argv[i], key))
            return argv[i + 1];
    return NULL;
}

int main(int argc, char **argv)
{
    /* Initialize output to stdout */
    io_init(STDOUT_FILENO);

    /* Install signal handlers for graceful termination */
    if (sig_install() != 0)
    {
        io_printf("Не удалось установить обработчики сигналов\n");
        return 1;
    }

    /* Initialize the simulation context */
    SimContext ctx;
    if (sim_init(&ctx) != 0)
    {
        io_printf("Ошибка инициализации контекста\n");
        return 1;
    }

    /* Load configuration from CLI args, config file, or interactive input */
    if (config_load(&ctx, argc, argv) != 0)
    {
        sim_free(&ctx);
        return 0; /* --help and similar — normal exit */
    }

    /* Open log file if --log was specified */
    const char *log_path = find_arg(argc, argv, "--log");
    if (log_path)
    {
        if (log_open(log_path) < 0)
        {
            io_printf("Не удалось открыть лог-файл: %s\n", log_path);
        }
        else
        {
            io_set_mirror(log_fd());
            io_printf("[LOG   ] зеркало журнала пишется в %s\n", log_path);
        }
    }

    /* Run the main simulation loop */
    int reason = sim_run(&ctx);

    /* Print global statistics */
    stats_print(&ctx.stats);

    /* Print per-gun statistics */
    io_printf("\nОрудия:\n");
    for (int i = 0; i < ctx.gun_count; ++i)
    {
        Oreshnik *g = &ctx.guns[i];
        io_printf("  #%d (%d,%d): выстрелов %lld, попаданий %lld, промахов %lld, остаток патронов %d\n",
                  g->id, g->x, g->y,
                  g->shots_fired, g->hits_direct, g->misses, g->ammo);
    }

    /* Print per-tank statistics */
    io_printf("Танки:\n");
    for (int i = 0; i < ctx.tank_count; ++i)
    {
        Tank *t = &ctx.tanks[i];
        io_printf("  #%d: %s, hp %d/%d, шагов %lld, попаданий по нему %lld\n",
                  t->id, t->alive ? "ВЫЖИЛ" : "УНИЧТОЖЕН",
                  t->hp, t->max_hp, t->moved_cells, t->shots_received);
    }

    io_printf("\nПричина завершения: %s\n", sim_reason_text(reason));

    /* Clean up resources */
    log_close();
    sim_free(&ctx);
    return 0;
}