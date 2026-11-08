#ifndef COORD_H
#define COORD_H

/**
 * \file coord.h
 * \brief The field coordinator process shared by implementations A, B, C.
 *
 * The coordinator owns the tick loop. Every tick it runs four phases in a
 * fixed order, waking the entity processes with their "go" semaphores and
 * waiting for reports through the counting "done" semaphore:
 *   1. tank moves; 2. gun observations; 3. gun shots; 4. projectile impacts.
 *
 * The way the entity processes are created and reaped differs between the
 * implementations; this module only implements the tick machine and the shutdown signalling.
 */

#include <semaphore.h>
#include "state.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * \brief Coordinator context: the state plus its synchronization objects
     */
    typedef struct
    {
        SharedState *st;         /**< Shared state (mapped) */
        sem_t *mutex;            /**< Binary semaphore: state access */
        sem_t *done;             /**< Counting semaphore: phase reports */
        sem_t *go[MAX_ENTITIES]; /**< Per-entity "go" semaphores */
    } Coord;

    /**
     * \brief Prints the startup banner with the main parameters
     * \param c Pointer to the coordinator context
     */
    void coord_banner(const Coord *c);

    /**
     * \brief Runs the tick loop of the simulation
     * \param c Pointer to the coordinator context
     * \return Termination reason code
     *
     * Checks the interrupt flag before every tick, so Ctrl+C (SIGINT) leads
     * to a correct REASON_INTERRUPTED termination with full IPC cleanup.
     */
    int coord_run(Coord *c);

    /**
     * \brief Asks all entity processes to finish
     * \param c Pointer to the coordinator context
     *
     * Sets the stop flag and wakes every entity (guns can wait on either of
     * their two phases, so they get two posts). Extra posts are harmless.
     */
    void coord_request_stop(Coord *c);

    /**
     * \brief Prints the final report: statistics and per-entity results
     * \param c Pointer to the coordinator context
     */
    void coord_print_results(const Coord *c);

#ifdef __cplusplus
}
#endif

#endif /* COORD_H */
