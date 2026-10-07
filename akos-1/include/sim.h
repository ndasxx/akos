#ifndef SIM_H
#define SIM_H

#include "common.h"
#include "event.h"
#include "field.h"
#include "tank.h"
#include "gun.h"
#include "projectile.h"
#include "stats.h"
#include "rng.h"

/**
 * \file sim.h
 * \brief Simulation context and main loop.
 */

/**
 * \brief Complete simulation context
 *
 * Contains everything needed by event handlers: field, entities, event queue,
 * RNG, statistics, and current time.
 */
typedef struct SimContext
{
    Field field;
    Tank tanks[MAX_TANKS];
    int tank_count;
    Oreshnik guns[MAX_GUNS];
    int gun_count;
    Projectile projectiles[MAX_PROJECTILES];

    EventQueue queue;
    Rng rng;
    SimStats stats;

    long long tick;      /**< Current model tick */
    long long max_ticks; /**< Tick limit (0 = unlimited) */
    int delay_ms;        /**< Delay between ticks, ms */
    int verbose_field;   /**< Print field state each tick */

    int reason; /**< Termination reason code (see below) */
} SimContext;

/**
 * \brief Termination reason codes
 */
enum
{
    REASON_RUNNING = 0,       /**< Simulation is still running */
    REASON_ALL_DESTROYED = 1, /**< All tanks destroyed */
    REASON_NO_AMMO = 2,       /**< All guns out of ammo */
    REASON_MAX_TICKS = 3,     /**< Tick limit reached */
    REASON_INTERRUPTED = 4,   /**< Interrupted by user (SIGINT/SIGTERM) */
    REASON_ERROR = 5          /**< Execution error */
};

/**
 * \brief Returns human-readable text for a termination reason
 * \param reason Reason code
 * \return Reason description string
 */
const char *sim_reason_text(int reason);

/**
 * \brief Initializes an empty simulation context
 * \param ctx Pointer to the context
 * \return 0 on success, -1 on error
 *
 * Parameters are filled by config.c.
 */
int sim_init(SimContext *ctx);

/**
 * \brief Frees resources held by the context
 * \param ctx Pointer to the context
 */
void sim_free(SimContext *ctx);

/**
 * \brief Schedules initial events (tank moves, gun observations, firing attempts)
 * \param ctx Pointer to the context
 * \return 0 on success, -1 on error
 *
 * Entities reschedule themselves after each event.
 */
int sim_schedule_initial(SimContext *ctx);

/**
 * \brief Main simulation loop
 * \param ctx Pointer to the context
 * \return Termination reason code
 *
 * Processes events from the queue, dispatches to handlers, checks termination
 * conditions at tick boundaries.
 */
int sim_run(SimContext *ctx);

/**
 * \brief Checks termination conditions at the end of a tick
 * \param ctx Pointer to the context
 * \return Termination reason code, or REASON_RUNNING if still active
 */
int sim_check_termination(SimContext *ctx);

#endif /* SIM_H */