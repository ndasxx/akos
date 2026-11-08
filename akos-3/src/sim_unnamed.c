/**
 * \file sim_unnamed.c
 * \brief Implementation B
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <semaphore.h>
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

/** \brief Name of the shared memory object */
#define SHM_NAME "/akos3b_shm"

/**
 * \brief Header of the shared segment
 */
typedef struct
{
    sem_t mutex;            /**< Protects SharedState */
    sem_t done;             /**< Phase completion reports */
    sem_t go[MAX_ENTITIES]; /**< Per-entity phase start */
    sem_t print;            /**< Output line serialization */
} ShmHeader;

/** \brief Complete shared segment: semaphores + state */
typedef struct
{
    ShmHeader hdr;
    SharedState st;
} ShmSegment;

static ShmSegment *g_seg = NULL; /* Mapped segment for cleanup */
static pid_t g_children[MAX_ENTITIES];
static int g_child_count = 0;

/* Destroys the shared segment (semaphores die together with the mapping). */
static void cleanup_all(void)
{
    if (!g_seg)
        return;
    sem_destroy(&g_seg->hdr.mutex);
    sem_destroy(&g_seg->hdr.done);
    sem_destroy(&g_seg->hdr.print);
    for (int i = 0; i < g_seg->st.tank_count + g_seg->st.gun_count; ++i)
        sem_destroy(&g_seg->hdr.go[i]);
    shm_destroy(g_seg, sizeof(ShmSegment), SHM_NAME);
    g_seg = NULL;
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

    /* One shared segment for semaphores + state */
    if (shm_create(SHM_NAME, sizeof(ShmSegment), (void **)&g_seg) != 0)
    {
        log_close();
        return 1;
    }
    memset(g_seg, 0, sizeof(*g_seg));
    if (state_build(&g_seg->st, &cfg) != STATUS_OK)
    {
        io_printf("Ошибка построения начального состояния\n");
        cleanup_all();
        log_close();
        return 1;
    }
    g_seg->st.coord_pid = getpid();

    /* Unnamed semaphores in shared memory, shared between processes */
    sem_init(&g_seg->hdr.mutex, 1, 1);
    sem_init(&g_seg->hdr.done, 1, 0);
    sem_init(&g_seg->hdr.print, 1, 1);
    for (int i = 0; i < g_seg->st.tank_count + g_seg->st.gun_count; ++i)
        sem_init(&g_seg->hdr.go[i], 1, 0);

    io_set_lock(&g_seg->hdr.print);

    /* Spawn one child process per entity */
    int total = g_seg->st.tank_count + g_seg->st.gun_count;
    for (int i = 0; i < g_seg->st.tank_count; ++i)
    {
        pid_t pid = fork();
        if (pid < 0)
        {
            io_printf("fork() не удался\n");
            g_seg->st.reason = REASON_ERROR;
            break;
        }
        if (pid == 0)
        {
            exit(tank_proc_main(&g_seg->st, &g_seg->hdr.mutex,
                                &g_seg->hdr.done, &g_seg->hdr.go[i], i));
        }
        g_children[g_child_count++] = pid;
    }
    for (int j = 0; j < g_seg->st.gun_count && g_seg->st.reason != REASON_ERROR; ++j)
    {
        int slot = g_seg->st.tank_count + j;
        pid_t pid = fork();
        if (pid < 0)
        {
            io_printf("fork() не удался\n");
            g_seg->st.reason = REASON_ERROR;
            break;
        }
        if (pid == 0)
            exit(gun_proc_main(&g_seg->st, &g_seg->hdr.mutex,
                               &g_seg->hdr.done, &g_seg->hdr.go[slot], j));
        g_children[g_child_count++] = pid;
    }

    /* Tick loop of the coordinator */
    Coord c = {.st = &g_seg->st, .mutex = &g_seg->hdr.mutex, .done = &g_seg->hdr.done};
    for (int i = 0; i < total; ++i)
        c.go[i] = &g_seg->hdr.go[i];

    if (g_seg->st.reason != REASON_ERROR)
        coord_run(&c);

    coord_request_stop(&c);
    for (int i = 0; i < g_child_count; ++i)
        waitpid(g_children[i], NULL, 0);

    coord_print_results(&c);

    /* Destroy semaphores + remove the shared memory object */
    cleanup_all();
    log_close();
    return 0;
}
