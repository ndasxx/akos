/**
 * \file logic.c
 * \brief Implementation of the simulation domain logic.
 */

#include "logic.h"
#include "io.h"
#include "field.h"
#include <stddef.h>

/* ------------------------------------------------------------------ */
/* Name mappings                                                      */
/* ------------------------------------------------------------------ */

const char *tank_strategy_name(tank_strategy_t s)
{
    switch (s)
    {
    case TANK_STAY:
        return "stay";
    case TANK_RANDOM_WALK:
        return "random_walk";
    case TANK_STRAIGHT:
        return "straight";
    case TANK_PATROL:
        return "patrol";
    }
    return "?";
}

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

int tank_slot_by_id(const SharedState *st, int tank_id)
{
    for (int i = 0; i < st->tank_count; ++i)
        if (st->tanks[i].id == tank_id)
            return i;
    return -1;
}

int gun_slot_by_id(const SharedState *st, int gun_id)
{
    for (int i = 0; i < st->gun_count; ++i)
        if (st->guns[i].id == gun_id)
            return i;
    return -1;
}

/* ------------------------------------------------------------------ */
/* Tanks: decision                                                    */
/* ------------------------------------------------------------------ */

static int can_step(const ShmField *f, const Tank *tanks, int n,
                    const Tank *self, int nx, int ny)
{
    if (!field_is_passable(f, nx, ny))
        return 0;
    for (int i = 0; i < n; ++i)
    {
        if (!tanks[i].alive || tanks[i].id == self->id)
            continue;
        if (tanks[i].x == nx && tanks[i].y == ny)
            return 0;
    }
    return 1;
}

int tank_decide_step(const ShmField *field, const Tank *tanks, int n,
                     Tank *t, Rng *rng, int *out_dx, int *out_dy)
{
    /* Direction vectors: stay, north, east, south, west */
    static const int DX[5] = {0, 0, 1, 0, -1};
    static const int DY[5] = {0, -1, 0, 1, 0};

    *out_dx = 0;
    *out_dy = 0;

    switch (t->strategy)
    {

    case TANK_STAY:
        return 1;

    case TANK_RANDOM_WALK:
    {
        int order[5] = {0, 1, 2, 3, 4};
        for (int i = 4; i > 0; --i)
        {
            int j = rng_int(rng, 0, i);
            int tmp = order[i];
            order[i] = order[j];
            order[j] = tmp;
        }
        for (int k = 0; k < 5; ++k)
        {
            int d = order[k];
            int nx = t->x + DX[d];
            int ny = t->y + DY[d];
            if (d == 0 || can_step(field, tanks, n, t, nx, ny))
            {
                *out_dx = DX[d];
                *out_dy = DY[d];
                return 1;
            }
        }
        return 1;
    }

    case TANK_STRAIGHT:
    {
        int nx = t->x + t->dx;
        int ny = t->y + t->dy;
        if ((t->dx || t->dy) && can_step(field, tanks, n, t, nx, ny))
        {
            *out_dx = t->dx;
            *out_dy = t->dy;
            return 1;
        }
        int perp[2][2];
        if (t->dx != 0)
        {
            perp[0][0] = 0;
            perp[0][1] = -1;
            perp[1][0] = 0;
            perp[1][1] = 1;
        }
        else if (t->dy != 0)
        {
            perp[0][0] = -1;
            perp[0][1] = 0;
            perp[1][0] = 1;
            perp[1][1] = 0;
        }
        else
        {
            perp[0][0] = 1;
            perp[0][1] = 0;
            perp[1][0] = -1;
            perp[1][1] = 0;
        }
        int first = rng_int(rng, 0, 1);
        for (int k = 0; k < 2; ++k)
        {
            int idx = (first + k) & 1;
            int ndx = perp[idx][0], ndy = perp[idx][1];
            if (can_step(field, tanks, n, t, t->x + ndx, t->y + ndy))
            {
                *out_dx = ndx;
                *out_dy = ndy;
                return 1;
            }
        }
        return 1;
    }

    case TANK_PATROL:
    {
        int tx = (t->patrol_leg == 0) ? t->patrol_x0 : t->patrol_x1;
        int ty = (t->patrol_leg == 0) ? t->patrol_y0 : t->patrol_y1;
        if (t->x == tx && t->y == ty)
        {
            t->patrol_leg ^= 1;
            tx = (t->patrol_leg == 0) ? t->patrol_x0 : t->patrol_x1;
            ty = (t->patrol_leg == 0) ? t->patrol_y0 : t->patrol_y1;
        }
        int ndx = 0, ndy = 0;
        if (t->x != tx)
            ndx = (tx > t->x) ? 1 : -1;
        else if (t->y != ty)
            ndy = (ty > t->y) ? 1 : -1;

        if (can_step(field, tanks, n, t, t->x + ndx, t->y + ndy))
        {
            *out_dx = ndx;
            *out_dy = ndy;
            return 1;
        }
        int alt_dx = 0, alt_dy = 0;
        if (t->y != ty)
            alt_dy = (ty > t->y) ? 1 : -1;
        if (can_step(field, tanks, n, t, t->x + alt_dx, t->y + alt_dy))
        {
            *out_dx = alt_dx;
            *out_dy = alt_dy;
            return 1;
        }
        return 1;
    }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Tanks: state updates                                               */
/* ------------------------------------------------------------------ */

void tank_apply_step(SharedState *st, int slot, int dx, int dy)
{
    Tank *t = &st->tanks[slot];
    if (!t->alive)
        return;

    if (dx == 0 && dy == 0)
    {
        io_printf("[TANK#%-2d] остаётся на месте (%d,%d)  [%s]\n",
                  t->id, t->x, t->y, tank_strategy_name(t->strategy));
    }
    else
    {
        int nx = t->x + dx;
        int ny = t->y + dy;
        io_printf("[TANK#%-2d] (%d,%d) -> (%d,%d)  [%s]\n",
                  t->id, t->x, t->y, nx, ny, tank_strategy_name(t->strategy));
        t->x = nx;
        t->y = ny;
        t->moved_cells++;
        if (t->strategy == TANK_STRAIGHT)
        {
            t->dx = dx;
            t->dy = dy;
        }
    }
}

void tank_apply_damage(SharedState *st, int tank_id, int dmg, const char *cause)
{
    int slot = tank_slot_by_id(st, tank_id);
    if (slot < 0)
        return;
    Tank *t = &st->tanks[slot];
    if (!t->alive)
        return;

    int before = t->hp;
    int after = before - dmg;
    int killed = 0;
    if (after <= 0)
    {
        after = 0;
        killed = 1;
    }
    t->hp = after;
    t->shots_received++;

    io_printf("[IMPACT]   танк #%d: hp %d -> %d  (%s)%s\n",
              t->id, before, after, cause,
              killed ? "  *** УНИЧТОЖЕН ***" : "");

    if (killed)
    {
        t->alive = 0;
        field_set_wreck(&st->field, t->x, t->y);
        st->stats.tanks_destroyed++;
        io_printf("[TANK#%-2d] УНИЧТОЖЕН на (%d,%d), клетка помечена как обломки\n",
                  t->id, t->x, t->y);
    }
}

/* ------------------------------------------------------------------ */
/* Guns: decision                                                     */
/* ------------------------------------------------------------------ */

static int manhattan(int x0, int y0, int x1, int y1)
{
    int dx = x0 > x1 ? x0 - x1 : x1 - x0;
    int dy = y0 > y1 ? y0 - y1 : y1 - y0;
    return dx + dy;
}

void gun_decide_observe(const ShmField *field, const Tank *tanks, int n,
                        int gx, int gy, int range,
                        int *has_target, int *target_id,
                        int *last_x, int *last_y,
                        int *prev_x, int *prev_y)
{
    (void)field;
    int best_d = range + 1;
    const Tank *best = NULL;
    for (int i = 0; i < n; ++i)
    {
        const Tank *t = &tanks[i];
        if (!t->alive)
            continue;
        int d = manhattan(gx, gy, t->x, t->y);
        if (d <= range && d < best_d)
        {
            best_d = d;
            best = t;
        }
    }

    if (best)
    {
        if (!*has_target || *target_id != best->id)
        {
            *prev_x = best->x;
            *prev_y = best->y;
        }
        else if (best->x != *last_x || best->y != *last_y)
        {
            *prev_x = *last_x;
            *prev_y = *last_y;
        }
        *has_target = 1;
        *target_id = best->id;
        *last_x = best->x;
        *last_y = best->y;
    }
    else
    {
        *has_target = 0;
    }
}

void gun_decide_aim(gun_strategy_t strategy,
                    int last_x, int last_y, int prev_x, int prev_y,
                    int accuracy, int w, int h, Rng *rng,
                    int *out_x, int *out_y)
{
    int ax = last_x;
    int ay = last_y;

    switch (strategy)
    {
    case GUN_AIM_LAST:
        break;
    case GUN_PREDICT:
        ax = last_x + (last_x - prev_x);
        ay = last_y + (last_y - prev_y);
        break;
    case GUN_RANDOM:
        ax = last_x + rng_int(rng, -2, 2);
        ay = last_y + rng_int(rng, -2, 2);
        break;
    }

    if (!rng_chance(rng, accuracy))
    {
        ax += rng_int(rng, -1, 1);
        ay += rng_int(rng, -1, 1);
    }

    if (ax < 0)
        ax = 0;
    if (ay < 0)
        ay = 0;
    if (ax >= w)
        ax = w - 1;
    if (ay >= h)
        ay = h - 1;

    *out_x = ax;
    *out_y = ay;
}

/* ------------------------------------------------------------------ */
/* Guns: state updates                                                */
/* ------------------------------------------------------------------ */

void gun_observe_update(SharedState *st, int slot,
                        int new_has_target, int new_target_id,
                        int new_last_x, int new_last_y,
                        int new_prev_x, int new_prev_y)
{
    Oreshnik *g = &st->guns[slot];

    int old_has = g->has_target;
    int old_target = g->target_id;
    int old_lx = g->last_x, old_ly = g->last_y;

    g->has_target = new_has_target;
    g->target_id = new_target_id;
    g->last_x = new_last_x;
    g->last_y = new_last_y;
    g->prev_x = new_prev_x;
    g->prev_y = new_prev_y;

    if (g->has_target)
    {
        if (!old_has || old_target != g->target_id)
        {
            io_printf("[GUN#%-2d ] цель захвачена: танк #%d в (%d,%d)  [дальность наблюдения %d]\n",
                      g->id, g->target_id, g->last_x, g->last_y, g->range);
        }
        else if (g->last_x != old_lx || g->last_y != old_ly)
        {
            io_printf("[GUN#%-2d ] цель #%d переместилась: (%d,%d) -> (%d,%d)\n",
                      g->id, g->target_id, old_lx, old_ly, g->last_x, g->last_y);
        }
    }
    else if (old_has)
    {
        io_printf("[GUN#%-2d ] цель потеряна (последняя позиция (%d,%d))\n",
                  g->id, old_lx, old_ly);
    }
}

void gun_apply_observe(SharedState *st, int slot)
{
    Oreshnik *g = &st->guns[slot];
    int has_target = g->has_target;
    int target_id = g->target_id;
    int last_x = g->last_x, last_y = g->last_y;
    int prev_x = g->prev_x, prev_y = g->prev_y;

    gun_decide_observe(&st->field, st->tanks, st->tank_count,
                       g->x, g->y, g->range,
                       &has_target, &target_id,
                       &last_x, &last_y, &prev_x, &prev_y);

    gun_observe_update(st, slot, has_target, target_id,
                       last_x, last_y, prev_x, prev_y);
}

int gun_apply_fire(SharedState *st, int slot, int ax, int ay)
{
    Oreshnik *g = &st->guns[slot];

    g->next_fire = st->tick + g->reload;

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

    io_printf("[GUN#%-2d ] цель #%d last=(%d,%d) prev=(%d,%d), прицел %s -> (%d,%d)\n",
              g->id, g->target_id,
              g->last_x, g->last_y, g->prev_x, g->prev_y,
              gun_strategy_name(g->strategy), ax, ay);

    /* Allocate a projectile from the pool */
    int pid = -1;
    for (int i = 0; i < MAX_PROJECTILES; ++i)
        if (!st->projectiles[i].alive)
        {
            pid = i;
            break;
        }
    if (pid < 0)
    {
        io_printf("[GUN#%-2d ] нет свободных снарядов — выстрел отменён\n", g->id);
        return 0;
    }

    Projectile *p = &st->projectiles[pid];
    p->id = pid;
    p->gun_id = g->id;
    p->x = ax;
    p->y = ay;
    p->damage_direct = g->damage_direct;
    p->damage_splash = g->damage_splash;
    p->blast_radius = g->blast_radius;
    p->impact_time = st->tick + g->shot_delay;
    p->alive = 1;

    g->ammo--;
    g->shots_fired++;
    st->stats.shots_total++;

    io_printf("[GUN#%-2d ] ВЫСТРЕЛ: снаряд #%d -> (%d,%d), разрыв в такт %lld, боезапас %d->%d\n",
              g->id, pid, ax, ay, (long long)p->impact_time, g->ammo + 1, g->ammo);
    return 1;
}

/* ------------------------------------------------------------------ */
/* Projectiles                                                        */
/* ------------------------------------------------------------------ */

void proj_apply_impact(SharedState *st, int proj_id)
{
    if (proj_id < 0 || proj_id >= MAX_PROJECTILES)
        return;
    Projectile *p = &st->projectiles[proj_id];
    if (!p->alive)
        return;

    io_printf("[IMPACT] снаряд #%d орудия #%d: разрыв в (%d,%d)\n",
              p->id, p->gun_id, p->x, p->y);

    int any = 0;
    for (int i = 0; i < st->tank_count; ++i)
    {
        Tank *t = &st->tanks[i];
        if (!t->alive)
            continue;

        int dx = t->x > p->x ? t->x - p->x : p->x - t->x;
        int dy = t->y > p->y ? t->y - p->y : p->y - t->y;
        int d = dx + dy;

        if (d == 0)
        {
            io_printf("[IMPACT]   фактическое положение: танк #%d в (%d,%d), дистанция 0\n",
                      t->id, t->x, t->y);
            io_printf("[IMPACT]   ПРЯМОЕ ПОПАДАНИЕ\n");
            tank_apply_damage(st, t->id, p->damage_direct, "direct");
            any = 1;
        }
        else if (d <= p->blast_radius)
        {
            io_printf("[IMPACT]   близкий разрыв: танк #%d в (%d,%d), дистанция %d\n",
                      t->id, t->x, t->y, d);
            tank_apply_damage(st, t->id, p->damage_splash, "splash");
            any = 1;
        }
    }

    int gslot = gun_slot_by_id(st, p->gun_id);
    if (!any)
    {
        io_printf("[IMPACT]   в клетке и в радиусе %d танков нет — ПРОМАХ\n",
                  p->blast_radius);
        if (gslot >= 0)
        {
            st->guns[gslot].misses++;
            st->stats.shots_missed++;
        }
    }
    else if (gslot >= 0)
    {
        st->guns[gslot].hits_direct++;
        st->stats.shots_hit++;
    }

    p->alive = 0;
}

/* ------------------------------------------------------------------ */
/* Termination                                                        */
/* ------------------------------------------------------------------ */

int sim_check_termination(SharedState *st)
{
    int alive_tanks = 0;
    for (int i = 0; i < st->tank_count; ++i)
        if (st->tanks[i].alive)
            alive_tanks++;

    if (st->tank_count > 0 && alive_tanks == 0)
        return REASON_ALL_DESTROYED;

    int ammo_total = 0;
    for (int i = 0; i < st->gun_count; ++i)
        ammo_total += st->guns[i].ammo;
    if (st->gun_count > 0 && ammo_total == 0)
    {
        int pending = 0;
        for (int i = 0; i < MAX_PROJECTILES; ++i)
            if (st->projectiles[i].alive)
                pending = 1;
        if (!pending)
            return REASON_NO_AMMO;
    }

    if (st->max_ticks > 0 && st->tick >= st->max_ticks)
        return REASON_MAX_TICKS;

    return REASON_RUNNING;
}
