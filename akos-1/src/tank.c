/**
 * \file tank.c
 * \brief Implementation of tank movement strategies and damage handling.
 */

#include "tank.h"
#include "sim.h"
#include "io.h"
#include "rng.h"
#include "event.h"

/* Map strategy enum to its string representation. */
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

/*
 * Check if any live tank (except ignore_id) occupies cell (x,y).
 * Used to prevent two tanks from occupying the same cell.
 */
int tank_is_occupied(const Tank *tanks, int n, int x, int y, int ignore_id)
{
    for (int i = 0; i < n; ++i)
    {
        if (!tanks[i].alive)
            continue;
        if (tanks[i].id == ignore_id)
            continue;
        if (tanks[i].x == x && tanks[i].y == y)
            return 1;
    }
    return 0;
}

/*
 * Check if a tank can step into cell (nx,ny): must be passable and
 * not occupied by another live tank.
 */
static int can_step(struct SimContext *ctx, const Tank *t, int nx, int ny)
{
    if (!field_is_passable(&ctx->field, nx, ny))
        return 0;
    if (tank_is_occupied(ctx->tanks, ctx->tank_count, nx, ny, t->id))
        return 0;
    return 1;
}

/*
 * Choose the next step for the tank according to its strategy.
 * Returns 1 if a valid step was found, 0 if the tank must stay.
 * The chosen delta is written to out_dx/out_dy.
 */
static int pick_step(struct SimContext *ctx, Tank *t, int *out_dx, int *out_dy)
{
    /* Direction vectors: stay, north, east, south, west*/
    static const int DX[5] = {0, 0, 1, 0, -1};
    static const int DY[5] = {0, -1, 0, 1, 0};

    switch (t->strategy)
    {

    case TANK_STAY:
        *out_dx = *out_dy = 0;
        return 1;

    case TANK_RANDOM_WALK:
    {
        /* Shuffle the 5 options (stay + 4 directions) and try them in order */
        int order[5] = {0, 1, 2, 3, 4};
        for (int i = 4; i > 0; --i)
        {
            int j = rng_int(&ctx->rng, 0, i);
            int tmp = order[i];
            order[i] = order[j];
            order[j] = tmp;
        }
        for (int k = 0; k < 5; ++k)
        {
            int d = order[k];
            int nx = t->x + DX[d];
            int ny = t->y + DY[d];
            if (d == 0 || can_step(ctx, t, nx, ny))
            {
                *out_dx = DX[d];
                *out_dy = DY[d];
                return 1;
            }
        }
        *out_dx = *out_dy = 0;
        return 1;
    }

    case TANK_STRAIGHT:
    {
        int nx = t->x + t->dx;
        int ny = t->y + t->dy;
        if ((t->dx || t->dy) && can_step(ctx, t, nx, ny))
        {
            *out_dx = t->dx;
            *out_dy = t->dy;
            return 1;
        }
        /* Blocked — turn 90° with random choice of side */
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
            /* No direction yet — pick a random initial direction */
            perp[0][0] = 1;
            perp[0][1] = 0;
            perp[1][0] = -1;
            perp[1][1] = 0;
        }
        int first = rng_int(&ctx->rng, 0, 1);
        for (int k = 0; k < 2; ++k)
        {
            int idx = (first + k) & 1;
            int ndx = perp[idx][0], ndy = perp[idx][1];
            if (can_step(ctx, t, t->x + ndx, t->y + ndy))
            {
                t->dx = *out_dx = ndx;
                t->dy = *out_dy = ndy;
                return 1;
            }
        }
        /* All blocked — stay, but remember the desired direction */
        *out_dx = *out_dy = 0;
        return 1;
    }

    case TANK_PATROL:
    {
        int tx = (t->patrol_leg == 0) ? t->patrol_x0 : t->patrol_x1;
        int ty = (t->patrol_leg == 0) ? t->patrol_y0 : t->patrol_y1;
        if (t->x == tx && t->y == ty)
        {
            /* Reached the waypoint — switch to the other leg */
            t->patrol_leg ^= 1;
            tx = (t->patrol_leg == 0) ? t->patrol_x0 : t->patrol_x1;
            ty = (t->patrol_leg == 0) ? t->patrol_y0 : t->patrol_y1;
        }
        /* First reduce |dx|, then |dy| */
        int ndx = 0, ndy = 0;
        if (t->x != tx)
            ndx = (tx > t->x) ? 1 : -1;
        else if (t->y != ty)
            ndy = (ty > t->y) ? 1 : -1;

        if (can_step(ctx, t, t->x + ndx, t->y + ndy))
        {
            *out_dx = ndx;
            *out_dy = ndy;
            return 1;
        }
        /* Try the other axis */
        int alt_dx = 0, alt_dy = 0;
        if (t->y != ty)
            alt_dy = (ty > t->y) ? 1 : -1;
        if (can_step(ctx, t, t->x + alt_dx, t->y + alt_dy))
        {
            *out_dx = alt_dx;
            *out_dy = alt_dy;
            return 1;
        }
        *out_dx = *out_dy = 0;
        return 1;
    }
    }
    *out_dx = *out_dy = 0;
    return 0;
}

int tank_handle_move(struct SimContext *ctx, int tank_id)
{
    Tank *t = NULL;
    for (int i = 0; i < ctx->tank_count; ++i)
    {
        if (ctx->tanks[i].id == tank_id)
        {
            t = &ctx->tanks[i];
            break;
        }
    }
    if (!t || !t->alive)
        return 0; /* Destroyed tanks do not move */

    int dx, dy;
    pick_step(ctx, t, &dx, &dy);

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
        /* For STRAIGHT, remember the direction */
        if (t->strategy == TANK_STRAIGHT)
        {
            t->dx = dx;
            t->dy = dy;
        }
    }

    /* Schedule the next move for tick+1 if the tank is still alive */
    if (t->alive)
    {
        if (eq_push(&ctx->queue, EV_TANK_MOVE, t->id, ctx->tick + 1) != 0)
            return -1;
    }
    return 0;
}

int tank_apply_damage(struct SimContext *ctx, int tank_id, int dmg, const char *cause)
{
    Tank *t = NULL;
    for (int i = 0; i < ctx->tank_count; ++i)
    {
        if (ctx->tanks[i].id == tank_id)
        {
            t = &ctx->tanks[i];
            break;
        }
    }
    if (!t || !t->alive)
        return 0;

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
        /* Mark the cell as a wreck (obstacle) */
        field_set_wreck(&ctx->field, t->x, t->y);
        ctx->stats.tanks_destroyed++;
        io_printf("[TANK#%-2d] УНИЧТОЖЕН на (%d,%d), клетка помечена как обломки\n",
                  t->id, t->x, t->y);
    }
    return 0;
}