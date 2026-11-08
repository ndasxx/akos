/**
 * \file sim_named.c
 * \brief Implementation A
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>

#include "common.h"
#include "state.h"
#include "cfg.h"
#include "coord.h"
#include "proc.h"
#include "logic.h"
#include "io.h"
#include "log.h"
#include "signals.h"
#include "shmutil.h"
#include "semutil.h"

/** \brief Name of the shared memory object */
#define SHM_NAME "/akos3a_shm"
/** \brief Binary semaphore protecting the shared state */
#define SEM_MUTEX "/akos3a_mutex"
/** \brief Counting semaphore of phase reports */
#define SEM_DONE "/akos3a_done"
/** \brief Binary semaphore serializing output */
#define SEM_PRINT "/akos3a_print"
/** \brief Prefix of the per-entity "go" semaphores */
#define SEM_GO_PREFIX "/akos3a_go"

/* Number of named semaphores besides the per-entity ones */
#define COMMON_SEMS 3

static sem_t *g_mutex; /* Opened semaphores for cleanup */
static sem_t *g_done;
static sem_t *g_print;
static sem_t *g_go[MAX_ENTITIES];
static char g_go_names[MAX_ENTITIES][64];
static int g_go_count = 0;
static SharedState *g_st = NULL;       /* Mapped state for cleanup */
static pid_t g_children[MAX_ENTITIES]; /* Child PIDs */
static int g_child_count = 0;

/* Opens the common semaphores and one "go" semaphore per entity. */
static int open_semaphores(const SharedState *st)
{
    if (sem_open_named(SEM_MUTEX, 1, &g_mutex) != 0)
        return -1;
    if (sem_open_named(SEM_DONE, 0, &g_done) != 0)
        return -1;
    if (sem_open_named(SEM_PRINT, 1, &g_print) != 0)
        return -1;

    int total = st->tank_count + st->gun_count;
    for (int i = 0; i < total; ++i)
    {
        sem_name_entity(g_go_names[i], sizeof(g_go_names[i]), SEM_GO_PREFIX, i);
        if (sem_open_named(g_go_names[i], 0, &g_go[i]) != 0)
            return -1;
        g_go_count++;
    }
    return 0;
}

/* Closes and unlinks every semaphore and the shared memory object. */
static void close_and_unlink_all(void)
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
    if (g_st)
        shm_destroy(g_st, sizeof(SharedState), SHM_NAME);
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

    /* Optional journal of results */
    if (cfg.log_path[0])
    {
        if (log_open(cfg.log_path) < 0)
            io_printf("Не удалось открыть лог-файл: %s\n", cfg.log_path);
        else
            io_set_mirror(log_fd());
    }

    /* Shared memory with the full simulation state */
    if (shm_create(SHM_NAME, sizeof(SharedState), (void **)&g_st) != 0)
    {
        log_close();
        return 1;
    }
    memset(g_st, 0, sizeof(*g_st));
    if (state_build(g_st, &cfg) != STATUS_OK)
    {
        io_printf("Ошибка построения начального состояния\n");
        shm_destroy(g_st, sizeof(SharedState), SHM_NAME);
        log_close();
        return 1;
    }
    g_st->coord_pid = getpid();

    /* Named semaphores */
    if (open_semaphores(g_st) != 0)
    {
        io_printf("Ошибка создания семафоров\n");
        close_and_unlink_all();
        log_close();
        return 1;
    }
    io_set_lock(g_print);

    /* Spawn one child process per entity */
    int total = g_st->tank_count + g_st->gun_count;
    for (int i = 0; i < g_st->tank_count; ++i)
    {
        pid_t pid = fork();
        if (pid < 0)
        {
            io_printf("fork() не удался\n");
            g_st->reason = REASON_ERROR;
            break;
        }
        if (pid == 0)
            exit(tank_proc_main(g_st, g_mutex, g_done, g_go[i], i));
        g_children[g_child_count++] = pid;
    }
    for (int j = 0; j < g_st->gun_count && g_st->reason != REASON_ERROR; ++j)
    {
        int slot = g_st->tank_count + j;
        pid_t pid = fork();
        if (pid < 0)
        {
            io_printf("fork() не удался\n");
            g_st->reason = REASON_ERROR;
            break;
        }
        if (pid == 0)
            exit(gun_proc_main(g_st, g_mutex, g_done, g_go[slot], j));
        g_children[g_child_count++] = pid;
    }

    /* Tick loop of the coordinator */
    Coord c = {.st = g_st, .mutex = g_mutex, .done = g_done};
    memcpy(c.go, g_go, sizeof(g_go));

    if (g_st->reason != REASON_ERROR)
        coord_run(&c);

    /* Ask the entities to finish and reap them */
    coord_request_stop(&c);
    for (int i = 0; i < g_child_count; ++i)
        waitpid(g_children[i], NULL, 0);

    /* Final report */
    coord_print_results(&c);

    /* Full IPC cleanup: close+unlink all semaphores, remove shared memory */
    close_and_unlink_all();
    log_close();
    (void)total;
    return 0;
}
