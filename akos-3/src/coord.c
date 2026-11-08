/**
 * \file coord.c
 * \brief Implementation of the field coordinator tick machine.
 */

#include "coord.h"
#include "logic.h"
#include "io.h"
#include "field.h"
#include "stats.h"
#include "semutil.h"
#include "signals.h"

#include <time.h>
#include <string.h>
#include <stdlib.h>

void coord_banner(const Coord *c)
{
    io_printf("=== Начало симуляции ===\n");
    io_printf("Поле: %dx%d, танков: %d, орудий: %d, max_ticks: %lld\n",
              c->st->field.w, c->st->field.h,
              c->st->tank_count, c->st->gun_count,
              c->st->max_ticks);
}

/* Delay between ticks using nanosleep (interrupted by signals). */
static void coord_delay(int ms)
{
    if (ms <= 0)
        return;
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

/* Verbose dump of the field state at the beginning of a tick. */
static void coord_dump_field(const Coord *c)
{
    if (!c->st->verbose_field)
        return;
    char buf[4096];
    field_print(&c->st->field, buf, sizeof(buf));
    io_printf("[FIELD ] состояние поля на такт %lld:\n%s",
              c->st->tick, buf);
}

/*
 * Run one entity phase.
 */
static void coord_phase(Coord *c, int kind)
{
    int begin = (kind == 0) ? 0 : c->st->tank_count;
    int end = (kind == 0) ? c->st->tank_count
                          : c->st->tank_count + c->st->gun_count;
    for (int i = begin; i < end; ++i)
        sem_post(c->go[i]);
    for (int i = begin; i < end; ++i)
    {
        for (;;)
        {
            int rc = sem_timedwait_sec(c->done, 1);
            if (rc == 0)
                break;
            if (sig_stop_requested() || c->st->stop)
                return;
        }
    }
}

/*
 * Impact phase: resolve all projectiles whose impact time has come.
 */
static void coord_impacts(Coord *c)
{
    sem_wait_intr(c->mutex);
    for (int i = 0; i < MAX_PROJECTILES; ++i)
        if (c->st->projectiles[i].alive &&
            c->st->projectiles[i].impact_time <= c->st->tick)
            proj_apply_impact(c->st, i);
    sem_post(c->mutex);
}

int coord_run(Coord *c)
{
    SharedState *st = c->st;
    coord_banner(c);

    long long tick = 0;
    while (st->tank_count + st->gun_count > 0)
    {
        if (sig_stop_requested())
        {
            st->reason = REASON_INTERRUPTED;
            break;
        }

        sem_wait_intr(c->mutex);
        st->tick = tick;
        sem_post(c->mutex);

        io_printf("\n=== Такт %lld ===\n", tick);
        coord_dump_field(c);
        coord_delay(st->delay_ms);

        coord_phase(c, 0); /* 1. tank moves */
        coord_phase(c, 1); /* 2. gun observations */
        coord_phase(c, 1); /* 3. gun shots */
        coord_impacts(c);  /* 4. projectile impacts */

        sem_wait_intr(c->mutex);
        int r = sim_check_termination(st);
        sem_post(c->mutex);
        if (r != REASON_RUNNING)
        {
            st->reason = r;
            break;
        }
        tick++;
    }

    if (st->reason == REASON_RUNNING)
    {
        /* Nothing to simulate (no tanks and no guns) */
        sem_wait_intr(c->mutex);
        int r = sim_check_termination(st);
        sem_post(c->mutex);
        st->reason = (r == REASON_RUNNING) ? REASON_MAX_TICKS : r;
    }

    st->stats.ticks_simulated = st->tick;
    io_printf("\n=== Симуляция завершена: %s ===\n",
              sim_reason_text(st->reason));
    return st->reason;
}

void coord_request_stop(Coord *c)
{
    SharedState *st = c->st;
    sem_wait_intr(c->mutex);
    st->stop = 1;
    sem_post(c->mutex);

    for (int i = 0; i < st->tank_count; ++i)
        sem_post(c->go[i]);
    for (int i = st->tank_count; i < st->tank_count + st->gun_count; ++i)
    {
        sem_post(c->go[i]);
        sem_post(c->go[i]);
    }
}

void coord_print_results(const Coord *c)
{
    const SharedState *st = c->st;

    stats_print(&st->stats);

    io_printf("\nОрудия:\n");
    for (int i = 0; i < st->gun_count; ++i)
    {
        const Oreshnik *g = &st->guns[i];
        io_printf("  #%d (%d,%d): выстрелов %lld, попаданий %lld, промахов %lld, остаток патронов %d\n",
                  g->id, g->x, g->y,
                  g->shots_fired, g->hits_direct, g->misses, g->ammo);
    }

    io_printf("Танки:\n");
    for (int i = 0; i < st->tank_count; ++i)
    {
        const Tank *t = &st->tanks[i];
        io_printf("  #%d: %s, hp %d/%d, шагов %lld, попаданий по нему %lld\n",
                  t->id, t->alive ? "ВЫЖИЛ" : "УНИЧТОЖЕН",
                  t->hp, t->max_hp, t->moved_cells, t->shots_received);
    }

    io_printf("\nПричина завершения: %s\n", sim_reason_text(st->reason));
}
