/**
 * \file coord_shm.c
 * \brief Coordinator process of the independent implementation C
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

#include "common.h"
#include "state.h"
#include "cfg.h"
#include "coord.h"
#include "logic.h"
#include "io.h"
#include "log.h"
#include "signals.h"
#include "shmutil.h"
#include "semutil.h"

/** \brief Shared memory object of implementation C */
#define SHM_NAME "/akos3c_shm"
/** \brief Semaphore family prefix */
#define NS_PREFIX "/akos3c"

#define SEM_MUTEX NS_PREFIX "_mutex"
#define SEM_DONE NS_PREFIX "_done"
#define SEM_PRINT NS_PREFIX "_print"
#define SEM_ADMIN NS_PREFIX "_admin"
#define SEM_GO_PREFIX NS_PREFIX "_go"

static sem_t *g_mutex;
static sem_t *g_done;
static sem_t *g_print;
static sem_t *g_admin;
static sem_t *g_go[MAX_ENTITIES];
static char g_go_names[MAX_ENTITIES][64];
static int g_go_count = 0;
static SharedState *g_st = NULL;

/* Closes and unlinks every named semaphore and removes the shared memory. */
static void cleanup_all(void)
{
    for (int i = 0; i < g_go_count; ++i)
    {
        sem_close(g_go[i]);
        sem_unlink(g_go_names[i]);
    }
    if (g_mutex)
    {
        sem_close(g_mutex);
        sem_unlink(SEM_MUTEX);
    }
    if (g_done)
    {
        sem_close(g_done);
        sem_unlink(SEM_DONE);
    }
    if (g_print)
    {
        sem_close(g_print);
        sem_unlink(SEM_PRINT);
    }
    if (g_admin)
    {
        sem_close(g_admin);
        sem_unlink(SEM_ADMIN);
    }
    if (g_st)
        shm_destroy(g_st, sizeof(SharedState), SHM_NAME);
}

/* Opens the semaphore family and one "go" semaphore per entity slot. */
static int open_semaphore_family(int total)
{
    if (sem_open_named(SEM_MUTEX, 1, &g_mutex) != 0)
        return -1;
    if (sem_open_named(SEM_DONE, 0, &g_done) != 0)
        return -1;
    if (sem_open_named(SEM_PRINT, 1, &g_print) != 0)
        return -1;
    if (sem_open_named(SEM_ADMIN, 0, &g_admin) != 0)
        return -1;
    for (int i = 0; i < total; ++i)
    {
        sem_name_entity(g_go_names[i], sizeof(g_go_names[i]), SEM_GO_PREFIX, i);
        if (sem_open_named(g_go_names[i], 0, &g_go[i]) != 0)
            return -1;
        g_go_count++;
    }
    return 0;
}

int main(int argc, char **argv)
{
    io_init(STDOUT_FILENO);
    if (sig_install() != 0)
    {
        io_printf("Не удалось установить обработчики сигналов\n");
        return 1;
    }

    /* Configuration */
    SimConfig cfg;
    cfg_defaults(&cfg);
    int rc = cfg_load(&cfg, argc, argv);
    if (rc == 1)
    {
        cfg_usage(argv[0]);
        return 0;
    }
    if (rc != 0)
        return 1;

    if (cfg.log_path[0])
    {
        if (log_open(cfg.log_path) < 0)
            io_printf("Не удалось открыть лог-файл: %s\n", cfg.log_path);
        else
            io_set_mirror(log_fd());
    }

    /* Shared state */
    if (shm_create(SHM_NAME, sizeof(SharedState), (void **)&g_st) != 0)
    {
        log_close();
        return 1;
    }
    memset(g_st, 0, sizeof(*g_st));
    if (state_build(g_st, &cfg) != STATUS_OK)
    {
        io_printf("Ошибка построения начального состояния\n");
        cleanup_all();
        log_close();
        return 1;
    }
    g_st->coord_pid = getpid();

    if (open_semaphore_family(g_st->tank_count + g_st->gun_count) != 0)
    {
        io_printf("Ошибка создания семафоров\n");
        cleanup_all();
        log_close();
        return 1;
    }
    io_set_lock(g_print);

    int total = g_st->tank_count + g_st->gun_count;
    io_printf("Ожидание подключения %d процессов-сущностей "
              "(ent_tank/ent_gun)...\n",
              total);
    for (int i = 0; i < total; ++i)
    {
        for (;;)
        {
            int w = sem_timedwait_sec(g_admin, 1);
            if (w == 0)
                break;
            if (sig_stop_requested())
            {
                g_st->reason = REASON_INTERRUPTED;
                break;
            }
        }
        if (g_st->reason == REASON_INTERRUPTED)
            break;
        io_printf("Подключено: %d/%d\n", i + 1, total);
    }

    Coord c = {.st = g_st, .mutex = g_mutex, .done = g_done};
    memcpy(c.go, g_go, sizeof(g_go));

    if (g_st->reason != REASON_INTERRUPTED)
        coord_run(&c);

    /* Finish the entities: stop flag + wake them, then SIGTERM survivors */
    coord_request_stop(&c);
    sleep(1);
    for (int i = 0; i < total; ++i)
    {
        pid_t pid = g_st->entity_pids[i];
        if (pid > 0 && pid_alive(pid))
            kill(pid, SIGTERM);
    }

    coord_print_results(&c);

    cleanup_all();
    log_close();
    return 0;
}
