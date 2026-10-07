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

/* Zero out the context and initialize the event queue. */
int sim_init(SimContext *ctx)
{
    memset(ctx, 0, sizeof(*ctx));
    if (eq_init(&ctx->queue, 64) != 0)
        return -1;
    return 0;
}

/* Free the event queue and field. */
void sim_free(SimContext *ctx)
{
    eq_free(&ctx->queue);
    field_free(&ctx->field);
}

/*
 * Schedule initial events at tick 0: tank moves, gun observations, and
 * firing attempts. Entities will reschedule themselves after each event.
 */
int sim_schedule_initial(SimContext *ctx)
{
    for (int i = 0; i < ctx->tank_count; ++i)
    {
        if (ctx->tanks[i].alive)
        {
            if (eq_push(&ctx->queue, EV_TANK_MOVE, ctx->tanks[i].id, 0) != 0)
                return -1;
        }
    }
    for (int i = 0; i < ctx->gun_count; ++i)
    {
        if (eq_push(&ctx->queue, EV_GUN_OBSERVE, ctx->guns[i].id, 0) != 0)
            return -1;
        if (eq_push(&ctx->queue, EV_GUN_FIRE, ctx->guns[i].id, 0) != 0)
            return -1;
    }
    return 0;
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
 * Main simulation loop. Processes events from the queue, dispatches to
 * handlers, checks termination at tick boundaries. Returns the reason code.
 */
int sim_run(SimContext *ctx)
{
    io_printf("=== Начало симуляции ===\n");
    io_printf("Поле: %dx%d, танков: %d, орудий: %d, max_ticks: %lld\n",
              ctx->field.w, ctx->field.h,
              ctx->tank_count, ctx->gun_count,
              ctx->max_ticks);

    if (sim_schedule_initial(ctx) != 0)
        return REASON_ERROR;

    long long last_printed_tick = -1;

    while (!eq_empty(&ctx->queue))
    {
        /* Check for user interrupt (Ctrl+C) */
        if (sig_stop_requested())
        {
            ctx->reason = REASON_INTERRUPTED;
            break;
        }

        Event e;
        if (eq_pop(&ctx->queue, &e) != 0)
            break;

        /* Tick boundary: finalize previous tick, start new one */
        if (e.time != last_printed_tick)
        {
            /* Check termination at the end of the previous tick */
            if (last_printed_tick >= 0)
            {
                ctx->tick = last_printed_tick;
                int r = sim_check_termination(ctx);
                if (r != REASON_RUNNING)
                {
                    ctx->reason = r;
                    break;
                }
            }
            last_printed_tick = e.time;
            ctx->tick = e.time;
            io_printf("\n=== Такт %lld ===\n", ctx->tick);
            sim_dump_field(ctx);
            sim_delay(ctx->delay_ms);
        }

        /* Dispatch the event to the appropriate handler */
        switch (e.type)
        {
        case EV_TANK_MOVE:
            tank_handle_move(ctx, e.entity_id);
            break;
        case EV_GUN_OBSERVE:
            gun_handle_observe(ctx, e.entity_id);
            break;
        case EV_GUN_FIRE:
            gun_handle_fire(ctx, e.entity_id);
            break;
        case EV_PROJECTILE_IMPACT:
            proj_handle_impact(ctx, e.entity_id);
            break;
        case EV_END_CHECK:
            break;
        }
    }

    /* Final termination check in case the loop exited normally */
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