/**
 * \file attach.c
 * \brief Implementation of the entity attach protocol (implementation C).
 */

#include "attach.h"
#include "shmutil.h"
#include "semutil.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define ATTACH_TIMEOUT_MS 10000

int entity_attach(const char *shm_name, const char *ns_prefix,
                  char role, int id, EntityAttach *out)
{
    char name[128];

    /* Shared state: created by the coordinator */
    if (shm_attach(shm_name, sizeof(SharedState), (void **)&out->st) != 0)
    {
        fprintf(stderr, "Ожидание запуска координатора (%s)...\n", shm_name);
        if (shm_attach_wait(shm_name, sizeof(SharedState),
                            ATTACH_TIMEOUT_MS, (void **)&out->st) != 0)
        {
            fprintf(stderr, "Координатор не появился за %d с — выход\n",
                    ATTACH_TIMEOUT_MS / 1000);
            return -1;
        }
    }

    /* Common semaphores (created by the coordinator) */
    snprintf(name, sizeof(name), "%s_mutex", ns_prefix);
    out->mutex = sem_open_wait(name, ATTACH_TIMEOUT_MS);
    if (out->mutex == SEM_FAILED)
    {
        fprintf(stderr, "Нет семафора %s — координатор не запущен?\n", name);
        munmap(out->st, sizeof(SharedState));
        return -1;
    }
    snprintf(name, sizeof(name), "%s_done", ns_prefix);
    out->done = sem_open_wait(name, ATTACH_TIMEOUT_MS);
    if (out->done == SEM_FAILED)
    {
        fprintf(stderr, "Нет семафора %s — координатор не запущен?\n", name);
        munmap(out->st, sizeof(SharedState));
        return -1;
    }
    snprintf(name, sizeof(name), "%s_admin", ns_prefix);
    out->admin = sem_open_wait(name, ATTACH_TIMEOUT_MS);
    if (out->admin == SEM_FAILED)
    {
        fprintf(stderr, "Нет семафора %s — координатор не запущен?\n", name);
        munmap(out->st, sizeof(SharedState));
        return -1;
    }

    /* Entity slot: tanks first, then guns */
    if (role == 't')
    {
        if (id < 1 || id > out->st->tank_count)
        {
            fprintf(stderr, "Некорректный id танка %d (танков: %d)\n",
                    id, out->st->tank_count);
            munmap(out->st, sizeof(SharedState));
            return -1;
        }
        out->slot = id - 1;
    }
    else
    {
        if (id < 1 || id > out->st->gun_count)
        {
            fprintf(stderr, "Некорректный id орудия %d (орудий: %d)\n",
                    id, out->st->gun_count);
            munmap(out->st, sizeof(SharedState));
            return -1;
        }
        out->slot = out->st->tank_count + id - 1;
    }

    /* Own "go" semaphore */
    char go_prefix[128];
    char go_name[160];
    snprintf(go_prefix, sizeof(go_prefix), "%s_go", ns_prefix);
    sem_name_entity(go_name, sizeof(go_name), go_prefix, out->slot);
    out->go = sem_open_wait(go_name, ATTACH_TIMEOUT_MS);
    if (out->go == SEM_FAILED)
    {
        fprintf(stderr, "Нет семафора %s — координатор не запущен?\n", go_name);
        munmap(out->st, sizeof(SharedState));
        return -1;
    }

    /* Publish the PID and report the attach */
    sem_wait_intr(out->mutex);
    out->st->entity_pids[out->slot] = getpid();
    sem_post(out->mutex);
    sem_post(out->admin);

    return 0;
}

void entity_detach(EntityAttach *ea)
{
    if (ea->go)
        sem_close(ea->go);
    if (ea->admin)
        sem_close(ea->admin);
    if (ea->done)
        sem_close(ea->done);
    if (ea->mutex)
        sem_close(ea->mutex);
    if (ea->st)
        munmap(ea->st, sizeof(SharedState));
    memset(ea, 0, sizeof(*ea));
}
