#ifndef SIM_H
#define SIM_H

#include <pthread.h>
#include "common.h"
#include "field.h"
#include "tank.h"
#include "gun.h"
#include "projectile.h"
#include "stats.h"
#include "rng.h"
#include "sync.h"

/**
 * \file sim.h
 * \brief Simulation context and main loop.
 */

/**
 * \brief Complete simulation context
 *
 * Contains everything needed by the threads: field, entities, RNG, statistics,
 * current time, the mutex that protects all of them and the semaphore with
 * which the entity threads report to the field coordinator.
 */
typedef struct SimContext
{
    Field field;
    Tank tanks[MAX_TANKS];
    int tank_count;
    Oreshnik guns[MAX_GUNS];
    int gun_count;
    Projectile projectiles[MAX_PROJECTILES];

    Rng rng;
    SimStats stats;

    long long tick;      /**< Current model tick */
    long long max_ticks; /**< Tick limit (0 = unlimited) */
    int delay_ms;        /**< Delay between ticks, ms */
    int verbose_field;   /**< Print field state each tick */

    int reason; /**< Termination reason code (see below) */

    pthread_mutex_t mutex; /**< Protects the whole context while threads work */
    Sema done;             /**< Posted by an entity thread when its phase is finished */
    int stop;              /**< Set (under the mutex) to ask all threads to finish */
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
 * \brief Initializes an empty simulation context (mutex and semaphore too)
 * \param ctx Pointer to the context
 * \return 0 on success, -1 on error
 *
 * Parameters are filled by config.c.
 */
int sim_init(SimContext *ctx);

/**
 * \brief Frees resources held by the context (destroys mutex and semaphore)
 * \param ctx Pointer to the context
 */
void sim_free(SimContext *ctx);

/**
 * \brief Checks if the threads were asked to finish
 * \param ctx Pointer to the context
 * \return Non-zero if the stop flag is set
 */
int sim_stopped(SimContext *ctx);

/**
 * \brief Main simulation loop
 * \param ctx Pointer to the context
 * \return Termination reason code
 *
 * The calling thread is the field coordinator: it starts the threads of the
 * tanks and guns, and every tick runs the phases in a fixed order (tank moves,
 * gun observations, gun shots, projectile impacts) waiting for all entity
 * threads between the phases. Then it checks termination and stops the threads.
 */
int sim_run(SimContext *ctx);

/**
 * \brief Checks termination conditions at the end of a tick
 * \param ctx Pointer to the context
 * \return Termination reason code, or REASON_RUNNING if still active
 */
int sim_check_termination(SimContext *ctx);

#endif /* SIM_H */