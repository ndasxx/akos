#ifndef ATTACH_H
#define ATTACH_H

/**
 * \file attach.h
 * \brief Attach protocol of an entity process to the coordinator in the
 *        independent-process implementation C.
 *
 * The coordinator creates the shared memory object and all named
 * semaphores, then waits until every expected entity has attached.
 * An entity program (ent_tank / ent_gun) is started independently
 * (from any terminal) and performs the following handshake:
 *   1. opens and maps the shared memory object (no O_CREAT!);
 *   2. opens the common semaphores and its own "go" semaphore by name;
 *   3. publishes its PID in the shared state under the mutex semaphore;
 *   4. posts the "/akos3c_admin" semaphore to signal "I am ready".
 * The coordinator waits on the admin semaphore tank_count + gun_count
 * times and only then starts the tick loop.
 */

#include <semaphore.h>
#include "state.h"

/**
 * \brief Everything an attached entity process needs
 */
typedef struct
{
    SharedState *st; /**< Mapped shared state */
    sem_t *mutex;    /**< State mutex semaphore */
    sem_t *done;     /**< Phase completion semaphore */
    sem_t *go;       /**< Own "go" semaphore */
    sem_t *admin;    /**< Admin (attach) semaphore */
    int slot;        /**< Entity slot in the shared state */
} EntityAttach;

/**
 * \brief Attaches this process to the coordinator
 * \param shm_name  Name of the shared memory object
 * \param ns_prefix Prefix of the semaphore family (e.g. "/akos3c")
 * \param role      't' = tank, 'g' = gun
 * \param id        Entity ID (1-based)
 * \param out       Output: attach context
 * \return 0 on success, -1 on error
 *
 * Publishes the process PID and signals the attach to the coordinator.
 */
int entity_attach(const char *shm_name, const char *ns_prefix,
                  char role, int id, EntityAttach *out);

/**
 * \brief Detaches an entity process (closes semaphores, unmaps memory)
 * \param ea Attach context
 *
 * The entity never unlinks IPC objects: they belong to the coordinator.
 */
void entity_detach(EntityAttach *ea);

#endif /* ATTACH_H */
