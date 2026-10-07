/**
 * \file gun.c
 * \brief Implementation of gun observation, aiming, and firing logic.
 */

#include "gun.h"
#include "sim.h"
#include "io.h"
#include "rng.h"
#include "event.h"
#include "tank.h"
#include "projectile.h"

#include <stdlib.h>

/* Map strategy enum to its string representation. */
const char *gun_strategy_name(gun_strategy_t s)
{
    switch (s)
    {
    case GUN_RANDOM:
        return "random";
    case GUN_AIM_LAST:
        return "last";
    case GUN_PREDICT:
        return "predict";
    }
    return "?";
}

/* Calculate Manhattan distance between two points. */
static int manhattan(int x0, int y0, int x1, int y1)
{
    int dx = x0 > x1 ? x0 - x1 : x1 - x0;
    int dy = y0 > y1 ? y0 - y1 : y1 - y0;
    return dx + dy;
}

int gun_handle_observe(struct SimContext *ctx, int gun_id)
{
    Oreshnik *g = NULL;
    for (int i = 0; i < ctx->gun_count; ++i)
    {
        if (ctx->guns[i].id == gun_id)
        {
            g = &ctx->guns[i];
            break;
        }
    }
    if (!g)
        return 0;

    int best_d = g->range + 1;
    const Tank *best = NULL;
    for (int i = 0; i < ctx->tank_count; ++i)
    {
        const Tank *t = &ctx->tanks[i];
        if (!t->alive)
            continue;
        int d = manhattan(g->x, g->y, t->x, t->y);
        if (d <= g->range && d < best_d)
        {
            best_d = d;
            best = t;
        }
    }

    if (best)
    {
        if (!g->has_target || g->target_id != best->id)
        {
            io_printf("[GUN#%-2d ] цель захвачена: танк #%d в (%d,%d), дистанция %d (<=%d)\n",
                      g->id, best->id, best->x, best->y, best_d, g->range);
            g->prev_x = best->x;
            g->prev_y = best->y;
        }
        else if (best->x != g->last_x || best->y != g->last_y)
        {
            io_printf("[GUN#%-2d ] цель #%d переместилась: (%d,%d) -> (%d,%d)\n",
                      g->id, best->id, g->last_x, g->last_y, best->x, best->y);
            g->prev_x = g->last_x;
            g->prev_y = g->last_y;
        }
        g->has_target = 1;
        g->target_id = best->id;
        g->last_x = best->x;
        g->last_y = best->y;
    }
    else
    {
        if (g->has_target)
        {
            io_printf("[GUN#%-2d ] цель потеряна (последняя позиция (%d,%d))\n",
                      g->id, g->last_x, g->last_y);
        }
        g->has_target = 0;
    }

    /* Schedule the next observation for tick+1 */
    if (eq_push(&ctx->queue, EV_GUN_OBSERVE, g->id, ctx->tick + 1) != 0)
        return -1;
    return 0;
}

/*
 * Choose the aim point according to the gun's strategy. Apply inaccuracy
 * (random offset with probability 100-accuracy%), then clamp to field bounds.
 */
void gun_choose_aim(const Oreshnik *g, struct SimContext *ctx, int *out_x, int *out_y)
{
    int ax = g->last_x;
    int ay = g->last_y;

    switch (g->strategy)
    {
    case GUN_AIM_LAST:
        break;
    case GUN_PREDICT:
    {
        /* Linear extrapolation: last + (last - prev) */
        int ex = g->last_x + (g->last_x - g->prev_x);
        int ey = g->last_y + (g->last_y - g->prev_y);
        ax = ex;
        ay = ey;
        break;
    }
    case GUN_RANDOM:
    {
        /* Random offset around last known position */
        int dx = rng_int(&ctx->rng, -2, 2);
        int dy = rng_int(&ctx->rng, -2, 2);
        ax = g->last_x + dx;
        ay = g->last_y + dy;
        break;
    }
    }

    /* Apply inaccuracy: with probability (100-accuracy)%, add random offset ±1 */
    if (!rng_chance(&ctx->rng, g->accuracy))
    {
        int jx = rng_int(&ctx->rng, -1, 1);
        int jy = rng_int(&ctx->rng, -1, 1);
        ax += jx;
        ay += jy;
    }

    /* Clamp to field bounds */
    if (ax < 0)
        ax = 0;
    if (ay < 0)
        ay = 0;
    if (ax >= ctx->field.w)
        ax = ctx->field.w - 1;
    if (ay >= ctx->field.h)
        ay = ctx->field.h - 1;

    *out_x = ax;
    *out_y = ay;
}

int gun_handle_fire(struct SimContext *ctx, int gun_id)
{
    Oreshnik *g = NULL;
    for (int i = 0; i < ctx->gun_count; ++i)
    {
        if (ctx->guns[i].id == gun_id)
        {
            g = &ctx->guns[i];
            break;
        }
    }
    if (!g)
        return 0;

    /* Schedule the next firing attempt (reload), regardless of whether we fire now */
    if (eq_push(&ctx->queue, EV_GUN_FIRE, g->id, ctx->tick + g->reload) != 0)
        return -1;

    if (g->ammo <= 0)
    {
        io_printf("[GUN#%-2d ] выстрел невозможен: боезапас исчерпан\n", g->id);
        return 0;
    }
    if (!g->has_target)
    {
        io_printf("[GUN#%-2d ] выстрел невозможен: цель не наблюдается\n", g->id);
        return 0;
    }

    int ax, ay;
    gun_choose_aim(g, ctx, &ax, &ay);

    io_printf("[GUN#%-2d ] цель #%d last=(%d,%d) prev=(%d,%d), прицел %s -> (%d,%d)\n",
              g->id, g->target_id,
              g->last_x, g->last_y, g->prev_x, g->prev_y,
              gun_strategy_name(g->strategy), ax, ay);

    /* Allocate a projectile from the pool */
    int pid = proj_alloc(ctx);
    if (pid < 0)
    {
        io_printf("[GUN#%-2d ] нет свободных снарядов — выстрел отменён\n", g->id);
        return 0;
    }
    Projectile *p = &ctx->projectiles[pid];
    p->id = pid;
    p->gun_id = g->id;
    p->x = ax;
    p->y = ay;
    p->damage_direct = g->damage_direct;
    p->damage_splash = g->damage_splash;
    p->blast_radius = g->blast_radius;
    p->impact_time = ctx->tick + g->shot_delay;
    p->alive = 1;

    g->ammo--;
    g->shots_fired++;
    ctx->stats.shots_total++;

    io_printf("[GUN#%-2d ] ВЫСТРЕЛ: снаряд #%d -> (%d,%d), разрыв в такт %lld, боезапас %d->%d\n",
              g->id, pid, ax, ay, (long long)p->impact_time, g->ammo + 1, g->ammo);

    /* Schedule the projectile impact */
    if (eq_push(&ctx->queue, EV_PROJECTILE_IMPACT, pid, p->impact_time) != 0)
        return -1;
    return 0;
}