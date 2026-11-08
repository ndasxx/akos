#ifndef PROC_H
#define PROC_H

/**
 * \file proc.h
 * \brief Entity process loops (tank and gun) shared by the shared-memory
 *        implementations A (fork + named semaphores), B (fork + unnamed
 *        semaphores in shared memory) and C (independent processes).
 *
 * Both loops follow the same phase protocol driven by the coordinator:
 * the entity waits on its own "go" semaphore, works with the shared state
 * under the binary "mutex" semaphore, then reports completion through the
 * counting "done" semaphore.
 *
 * The waits use a timeout: if the coordinator dies or asks
 * everyone to stop, the entity notices within a second and exits cleanly.
 */

#include <semaphore.h>
#include "state.h"

/**
 * \brief Main loop of a tank process
 * \param st     Pointer to the shared state (mapped in this process)
 * \param mutex  Binary semaphore protecting the shared state
 * \param done   Counting semaphore to report phase completion
 * \param go     Per-entity semaphore posted by the coordinator at each phase
 * \param slot   Index of the tank in st->tanks
 * \return 0 when the entity finishes (stop flag, dead coordinator or signal)
 */
int tank_proc_main(SharedState *st, sem_t *mutex, sem_t *done,
                   sem_t *go, int slot);

/**
 * \brief Main loop of a gun process
 * \param st     Pointer to the shared state (mapped in this process)
 * \param mutex  Binary semaphore protecting the shared state
 * \param done   Counting semaphore to report phase completion
 * \param go     Per-entity semaphore posted by the coordinator at each phase
 * \param slot   Index of the gun in st->guns
 * \return 0 when the entity finishes
 *
 * Every tick the gun works in two phases (observation, then firing).
 */
int gun_proc_main(SharedState *st, sem_t *mutex, sem_t *done,
                  sem_t *go, int slot);

#endif /* PROC_H */
