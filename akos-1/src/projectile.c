/**
 * \file projectile.c
 * \brief Implementation of projectile allocation and impact resolution.
 */

#include "projectile.h"
#include "sim.h"
#include "io.h"
#include "tank.h"

/*
 * Find the first free projectile slot (alive == 0).
 */
int proj_alloc(struct SimContext *ctx)
{
    for (int i = 0; i < MAX_PROJECTILES; ++i)
    {
        if (!ctx->projectiles[i].alive)
            return i;
    }
    return -1;
}

/*
 * Resolve projectile impact at cell (x,y).
 * - Direct hit: distance 0 → damage_direct
 * - Splash: distance 1..blast_radius → damage_splash
 * - Miss: distance > blast_radius → no damage
 *
 * Multiple tanks can be hit by a single impact if they are within range.
 * Updates hit/miss statistics for the gun that fired the projectile.
 */
int proj_handle_impact(struct SimContext *ctx, int proj_id)
{
    if (proj_id < 0 || proj_id >= MAX_PROJECTILES)
        return -1;
    Projectile *p = &ctx->projectiles[proj_id];
    if (!p->alive)
        return 0;

    io_printf("[IMPACT] снаряд #%d орудия #%d: разрыв в (%d,%d)\n",
              p->id, p->gun_id, p->x, p->y);

    /* Check all tanks for hits */
    int any = 0;
    for (int i = 0; i < ctx->tank_count; ++i)
    {
        Tank *t = &ctx->tanks[i];
        if (!t->alive)
            continue;

        /* Distance from tank to impact point */
        int dx = t->x > p->x ? t->x - p->x : p->x - t->x;
        int dy = t->y > p->y ? t->y - p->y : p->y - t->y;
        int d = dx + dy;

        if (d == 0)
        {
            io_printf("[IMPACT]   фактическое положение: танк #%d в (%d,%d), дистанция 0\n",
                      t->id, t->x, t->y);
            io_printf("[IMPACT]   ПРЯМОЕ ПОПАДАНИЕ\n");
            tank_apply_damage(ctx, t->id, p->damage_direct, "direct");
            any = 1;
        }
        else if (d <= p->blast_radius)
        {
            io_printf("[IMPACT]   близкий разрыв: танк #%d в (%d,%d), дистанция %d\n",
                      t->id, t->x, t->y, d);
            tank_apply_damage(ctx, t->id, p->damage_splash, "splash");
            any = 1;
        }
    }

    if (!any)
    {
        io_printf("[IMPACT]   в клетке и в радиусе %d танков нет — ПРОМАХ\n",
                  p->blast_radius);
        /* Increment miss counter for the gun */
        for (int i = 0; i < ctx->gun_count; ++i)
        {
            if (ctx->guns[i].id == p->gun_id)
            {
                ctx->guns[i].misses++;
                ctx->stats.shots_missed++;
                break;
            }
        }
    }
    else
    {
        /* At least one hit — increment hit counter for the gun */
        for (int i = 0; i < ctx->gun_count; ++i)
        {
            if (ctx->guns[i].id == p->gun_id)
            {
                ctx->guns[i].hits_direct++; /* Both direct and splash count here */
                ctx->stats.shots_hit++;
                break;
            }
        }
    }

    /* Mark the projectile as spent */
    p->alive = 0;
    return 0;
}