/**
 * \file sim.c
 * \brief Implementation of the simulation context and main loop.
 */

#include "sim.h"
#include "io.h"
#include "field.h"
#include "signals.h"

#include <unistd.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>

/* Map termination reason code to text. */
const char *sim_reason_text(int reason)
{
    switch (reason)
    {
    case REASON_ALL_DESTROYED:
        return "все танки уничтожены";
    case REASON_NO_AMMO:
        return "боезапас всех орудий исчерпан";
    case REASON_MAX_TICKS:
        return "достигнут предел тактов";
    case REASON_INTERRUPTED:
        return "прервано пользователем";
    case REASON_ERROR:
        return "ошибка выполнения";
    default:
        return "выполняется";
    }
}

/* Zero out the context, create the mutex and the semaphore of the coordinator. */
int sim_init(SimContext *ctx)
{
    memset(ctx, 0, sizeof(*ctx));
    if (pthread_mutex_init(&ctx->mutex, NULL) != 0)
        return -1;
    if (sema_init(&ctx->done, 0) != 0)
    {
        pthread_mutex_destroy(&ctx->mutex);
        return -1;
    }
    return 0;
}

/* Free the field, destroy the mutex and the semaphore. */
void sim_free(SimContext *ctx)
{
    sema_destroy(&ctx->done);
    pthread_mutex_destroy(&ctx->mutex);
    field_free(&ctx->field);
}

/* Read the stop flag under the mutex: it is set by the coordinator. */
int sim_stopped(SimContext *ctx)
{
    pthread_mutex_lock(&ctx->mutex);
    int stop = ctx->stop;
    pthread_mutex_unlock(&ctx->mutex);
    return stop;
}

/*
 * Check termination conditions after each tick. Returns the reason code
 * if simulation should stop, or REASON_RUNNING if it should continue.
 */
int sim_check_termination(SimContext *ctx)
{
    /* All tanks destroyed? */
    int alive_tanks = 0;
    for (int i = 0; i < ctx->tank_count; ++i)
        if (ctx->tanks[i].alive)
            alive_tanks++;

    if (ctx->tank_count > 0 && alive_tanks == 0)
        return REASON_ALL_DESTROYED;

    /* All guns out of ammo? */
    int ammo_total = 0;
    for (int i = 0; i < ctx->gun_count; ++i)
        ammo_total += ctx->guns[i].ammo;
    if (ctx->gun_count > 0 && ammo_total == 0)
    {
        /* Wait for in-flight projectiles to land before terminating */
        int pending = 0;
        for (int i = 0; i < MAX_PROJECTILES; ++i)
            if (ctx->projectiles[i].alive)
                pending = 1;
        if (!pending)
            return REASON_NO_AMMO;
    }

    /* Tick limit reached? */
    if (ctx->max_ticks > 0 && ctx->tick >= ctx->max_ticks)
        return REASON_MAX_TICKS;

    return REASON_RUNNING;
}

/* Simple delay between ticks using nanosleep. */
static void sim_delay(int ms)
{
    if (ms <= 0)
        return;
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

/*
 * Print the field state as a text dump. Only called when verbose_field
 * is enabled (--verbose-field flag).
 */
static void sim_dump_field(SimContext *ctx)
{
    if (!ctx->verbose_field)
        return;
    char buf[4096];
    field_print(&ctx->field, buf, sizeof(buf));
    io_printf("[FIELD ] состояние поля на такт %lld:\n%s",
              ctx->tick, buf);
}

/*
 * Start a phase for several entities: wake each of them with its semaphore,
 * then wait until all of them have reported through ctx->done.
 * Entities of one phase work concurrently (the mutex protects the context).
 */
static void sim_phase_tanks(SimContext *ctx)
{
    for (int i = 0; i < ctx->tank_count; ++i)
        sema_post(&ctx->tanks[i].go);
    for (int i = 0; i < ctx->tank_count; ++i)
        sema_wait(&ctx->done);
}

static void sim_phase_guns(SimContext *ctx)
{
    for (int i = 0; i < ctx->gun_count; ++i)
        sema_post(&ctx->guns[i].go);
    for (int i = 0; i < ctx->gun_count; ++i)
        sema_wait(&ctx->done);
}

/*
 * Impact phase: the field coordinator resolves all projectiles whose impact
 * time has come. It runs after all moves of the tick, so a hit is decided by
 * the state of the field at the moment of the impact.
 */
static void sim_phase_impacts(SimContext *ctx)
{
    pthread_mutex_lock(&ctx->mutex);
    for (int i = 0; i < MAX_PROJECTILES; ++i)
    {
        if (ctx->projectiles[i].alive && ctx->projectiles[i].impact_time <= ctx->tick)
            proj_handle_impact(ctx, i);
    }
    pthread_mutex_unlock(&ctx->mutex);
}

/*
 * Create the threads of all tanks and guns. Every entity gets its own random
 * generator (derived from the main seed), so the result does not depend on the
 * order in which the threads run. Returns the number of started threads;
 * if it is less than tank_count + gun_count, thread creation failed.
 */
static int sim_start_threads(SimContext *ctx, pthread_t *threads)
{
    unsigned int base = ctx->rng.state;
    int started = 0;
    for (int i = 0; i < ctx->tank_count; ++i)
    {
        Tank *t = &ctx->tanks[i];
        t->ctx = ctx;
        rng_seed(&t->rng, base ^ (0x9E3779B9u * (unsigned int)(t->id + 1)));
        if (sema_init(&t->go, 0) != 0)
            return started;
        if (pthread_create(&threads[started], NULL, tank_thread, t) != 0)
        {
            sema_destroy(&t->go);
            return started;
        }
        started++;
    }
    for (int i = 0; i < ctx->gun_count; ++i)
    {
        Oreshnik *g = &ctx->guns[i];
        g->ctx = ctx;
        g->next_fire = 0;
        rng_seed(&g->rng, base ^ (0x85EBCA6Bu * (unsigned int)(g->id + 1)));
        if (sema_init(&g->go, 0) != 0)
            return started;
        if (pthread_create(&threads[started], NULL, gun_thread, g) != 0)
        {
            sema_destroy(&g->go);
            return started;
        }
        started++;
    }
    return started;
}

/*
 * Ask all threads to finish: set the stop flag under the mutex, wake every
 * entity (they are all waiting for the next phase), then join them.
 */
static void sim_stop_threads(SimContext *ctx, pthread_t *threads, int started)
{
    pthread_mutex_lock(&ctx->mutex);
    ctx->stop = 1;
    pthread_mutex_unlock(&ctx->mutex);
    for (int i = 0; i < ctx->tank_count; ++i)
        sema_post(&ctx->tanks[i].go);
    for (int i = 0; i < ctx->gun_count; ++i)
        sema_post(&ctx->guns[i].go);
    for (int i = 0; i < started; ++i)
        pthread_join(threads[i], NULL);
    for (int i = 0; i < ctx->tank_count; ++i)
        sema_destroy(&ctx->tanks[i].go);
    for (int i = 0; i < ctx->gun_count; ++i)
        sema_destroy(&ctx->guns[i].go);
}

/*
 * Main simulation loop (field coordinator). Every tick runs four phases in
 * a fixed order: tank moves, gun observations, gun shots, projectile impacts.
 * Returns the reason code.
 */
int sim_run(SimContext *ctx)
{
    io_printf("=== Начало симуляции ===\n");
    io_printf("Поле: %dx%d, танков: %d, орудий: %d, max_ticks: %lld\n",
              ctx->field.w, ctx->field.h,
              ctx->tank_count, ctx->gun_count,
              ctx->max_ticks);

    int total = ctx->tank_count + ctx->gun_count;
    pthread_t threads[MAX_TANKS + MAX_GUNS];
    int started = sim_start_threads(ctx, threads);
    if (started != total)
    {
        sim_stop_threads(ctx, threads, started);
        return REASON_ERROR;
    }

    long long tick = 0;
    while (total > 0)
    {
        if (sig_stop_requested())
        {
            ctx->reason = REASON_INTERRUPTED;
            break;
        }

        ctx->tick = tick;
        io_printf("\n=== Такт %lld ===\n", ctx->tick);
        sim_dump_field(ctx);
        sim_delay(ctx->delay_ms);

        sim_phase_tanks(ctx);   /* 1. moves */
        sim_phase_guns(ctx);    /* 2. observations */
        sim_phase_guns(ctx);    /* 3. shots */
        sim_phase_impacts(ctx); /* 4. impacts */

        /* Check termination at the end of the tick */
        pthread_mutex_lock(&ctx->mutex);
        int r = sim_check_termination(ctx);
        pthread_mutex_unlock(&ctx->mutex);
        if (r != REASON_RUNNING)
        {
            ctx->reason = r;
            break;
        }
        tick++;
    }

    sim_stop_threads(ctx, threads, started);

    /* Nothing to simulate (no tanks and no guns) */
    if (ctx->reason == REASON_RUNNING)
    {
        int r = sim_check_termination(ctx);
        ctx->reason = (r == REASON_RUNNING) ? REASON_MAX_TICKS : r;
    }

    ctx->stats.ticks_simulated = ctx->tick;
    io_printf("\n=== Симуляция завершена: %s ===\n",
              sim_reason_text(ctx->reason));
    return ctx->reason;
}
