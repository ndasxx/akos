/**
 * \file proc.c
 * \brief Implementation of the entity process loops.
 */

#include "proc.h"
#include "logic.h"
#include "rng.h"
#include "semutil.h"
#include "signals.h"

#include <stdlib.h>
#include <unistd.h>

/*
 * Seeds the per-entity generator deterministically from the base seed,
 * so the behaviour is reproducible regardless of process scheduling.
 */
static void seed_entity_rng(Rng *rng, const SharedState *st, int id, unsigned int salt)
{
    rng_seed(rng, st->seed ^ (salt * (unsigned int)(id + 1)));
}

/*
 * Wait for the next phase. Returns:
 *   1  - phase started (go posted)
 *   0  - asked to stop, coordinator dead, or signal received: entity exits
 */
static int wait_phase(sem_t *go, const SharedState *st)
{
    for (;;)
    {
        int rc = sem_timedwait_sec(go, 1);
        if (rc == 0)
            return 1;
        if (rc < 0)
            return 0;
        /* Timeout: check the stop flag and the coordinator liveness */
        if (st->stop || sig_stop_requested() || !pid_alive(st->coord_pid))
            return 0;
    }
}

int tank_proc_main(SharedState *st, sem_t *mutex, sem_t *done,
                   sem_t *go, int slot)
{
    Rng rng;
    seed_entity_rng(&rng, st, st->tanks[slot].id, 0x9E3779B9u);

    while (wait_phase(go, st))
    {
        if (st->stop || sig_stop_requested())
            break;
        sem_wait_intr(mutex);
        if (st->tanks[slot].alive)
        {
            int dx, dy;
            tank_decide_step(&st->field, st->tanks, st->tank_count,
                             &st->tanks[slot], &rng, &dx, &dy);
            tank_apply_step(st, slot, dx, dy);
        }
        sem_post(mutex);
        sem_post(done);
    }
    return 0;
}

int gun_proc_main(SharedState *st, sem_t *mutex, sem_t *done,
                  sem_t *go, int slot)
{
    Rng rng;
    seed_entity_rng(&rng, st, st->guns[slot].id, 0x85EBCA6Bu);

    for (;;)
    {
        /* Observation phase */
        if (!wait_phase(go, st))
            break;
        if (st->stop || sig_stop_requested())
            break;
        sem_wait_intr(mutex);
        gun_apply_observe(st, slot);
        sem_post(mutex);
        sem_post(done);

        /* Firing phase (only when the reload time has passed) */
        if (!wait_phase(go, st))
            break;
        if (st->stop || sig_stop_requested())
            break;
        sem_wait_intr(mutex);
        if (st->tick >= st->guns[slot].next_fire)
        {
            int ax, ay;
            Oreshnik *g = &st->guns[slot];
            gun_decide_aim(g->strategy, g->last_x, g->last_y,
                           g->prev_x, g->prev_y, g->accuracy,
                           st->field.w, st->field.h, &rng, &ax, &ay);
            gun_apply_fire(st, slot, ax, ay);
        }
        sem_post(mutex);
        sem_post(done);
    }
    return 0;
}
