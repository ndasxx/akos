/**
 * \file coord_fifo.c
 * \brief Coordinator process of implementation D
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <errno.h>

#include "common.h"
#include "state.h"
#include "cfg.h"
#include "coord.h"
#include "logic.h"
#include "io.h"
#include "log.h"
#include "signals.h"
#include "semutil.h"
#include "msg.h"
#include "field.h"

#include <time.h>

/** \brief Semaphore family prefix of implementation D */
#define NS_PREFIX "/akos3d"

#define SEM_PRINT NS_PREFIX "_print"
#define SEM_DONE NS_PREFIX "_done"
#define SEM_ADMIN NS_PREFIX "_admin"
#define SEM_READY_PREFIX NS_PREFIX "_ready"

/** \brief Maximum simultaneous entities (also the FIFO table size) */
#define MAX_ENT (MAX_TANKS + MAX_GUNS)

typedef struct
{
    char role;    /**< 't' = tank, 'g' = gun */
    int id;       /**< Entity ID (1-based) */
    int slot;     /**< Slot index (tanks first) */
    pid_t pid;    /**< Entity PID from the attach message */
    int req_fd;   /**< Request channel (entity -> coordinator) */
    int rep_fd;   /**< Reply channel (coordinator -> entity) */
    sem_t *ready; /**< Per-entity "message ready" semaphore */
    char ready_name[64];
} EntConn;

static sem_t *g_print;
static sem_t *g_done;
static sem_t *g_admin;
static EntConn g_ent[MAX_ENT];
static int g_ent_count = 0;
static SharedState *g_st = NULL; /**< Local (heap) state, never shared */

/* Removes every semaphore and FIFO created by the coordinator. */
static void cleanup_all(void)
{
    for (int i = 0; i < g_ent_count; ++i)
    {
        char p[128];
        if (g_ent[i].ready)
        {
            sem_close(g_ent[i].ready);
            sem_unlink(g_ent[i].ready_name);
        }
        if (g_ent[i].req_fd >= 0)
            close(g_ent[i].req_fd);
        if (g_ent[i].rep_fd >= 0)
            close(g_ent[i].rep_fd);
        fifo_path(p, sizeof(p), "req", g_ent[i].role == 't' ? "tank" : "gun", g_ent[i].id);
        unlink(p);
        fifo_path(p, sizeof(p), "rep", g_ent[i].role == 't' ? "tank" : "gun", g_ent[i].id);
        unlink(p);
    }
    if (g_print)
    {
        sem_close(g_print);
        sem_unlink(SEM_PRINT);
    }
    if (g_done)
    {
        sem_close(g_done);
        sem_unlink(SEM_DONE);
    }
    if (g_admin)
    {
        sem_close(g_admin);
        sem_unlink(SEM_ADMIN);
    }
    free(g_st);
    g_st = NULL;
}

/* Creates the FIFO pair of an entity (ignores "already exists"). */
static int mk_chan(const char *role, int id)
{
    char p[128];
    fifo_path(p, sizeof(p), "req", role, id);
    if (mkfifo(p, 0666) == -1 && errno != EEXIST)
        return -1;
    fifo_path(p, sizeof(p), "rep", role, id);
    if (mkfifo(p, 0666) == -1 && errno != EEXIST)
        return -1;
    return 0;
}

/* Opens the ready semaphore of an entity. */
static int open_ready(EntConn *e)
{
    char role = e->role;
    char rp[32];
    snprintf(rp, sizeof(rp), "%s_%s", SEM_READY_PREFIX,
             role == 't' ? "tank" : "gun");
    sem_name_entity(e->ready_name, sizeof(e->ready_name), rp, e->id);
    return sem_open_named(e->ready_name, 0, &e->ready);
}

static int attach_one(EntConn *e)
{
    char req[128];
    fifo_path(req, sizeof(req), "req", e->role == 't' ? "tank" : "gun", e->id);

    /* Waiting for the entity to appear: open blocks until its writer end. */
    for (;;)
    {
        e->req_fd = open(req, O_RDONLY);
        if (e->req_fd >= 0)
            break;
        if (errno != EINTR || sig_stop_requested())
            return -1;
    }

    Msg m;
    if (read_full(e->req_fd, &m, sizeof(m)) != 0)
        return -1;
    if (m.phase != PH_ATTACH)
        return -1;
    e->pid = m.pid;

    /* The ready semaphore of this entity was created at startup (the
     * entity opens it before posting admin, so it must already exist). */

    for (;;)
    {
        int w = sem_timedwait_sec(g_admin, 1);
        if (w == 0)
            break;
        if (w < 0 || sig_stop_requested())
            return -1;
    }

    char rep[128];
    fifo_path(rep, sizeof(rep), "rep", e->role == 't' ? "tank" : "gun", e->id);
    e->rep_fd = open(rep, O_WRONLY);
    if (e->rep_fd < 0)
        return -1;

    memset(&m, 0, sizeof(m));
    m.phase = PH_ATTACH;
    m.pid = getpid();
    m.seed = g_st->seed;
    if (write_full(e->rep_fd, &m, sizeof(m)) != 0)
        return -1;

    return 0;
}

/* Fills the common part of a context message. */
static void fill_ctx(Msg *m, int phase, const EntConn *e)
{
    memset(m, 0, sizeof(*m));
    m->phase = phase;
    m->id = e->id;
    m->pid = getpid();
    m->tick = g_st->tick;
    m->seed = g_st->seed;
    m->field = g_st->field;
    memcpy(m->tanks, g_st->tanks, sizeof(m->tanks));
    m->tank_count = g_st->tank_count;
    if (e->role == 'g')
        m->gun = g_st->guns[e->slot - g_st->tank_count];
}

/* Verifies that no live tank except ignore_id occupies the cell. */
static int cell_occupied(const SharedState *st, int x, int y, int ignore_id)
{
    for (int i = 0; i < st->tank_count; ++i)
    {
        const Tank *t = &st->tanks[i];
        if (t->alive && t->id != ignore_id && t->x == x && t->y == y)
            return 1;
    }
    return 0;
}

/* Waits for the answer of one entity with SIGINT awareness. */
static int wait_done(void)
{
    for (;;)
    {
        int rc = sem_timedwait_sec(g_done, 1);
        if (rc == 0)
            return 0;
        if (sig_stop_requested())
            return -1;
    }
}

/*
 * Sends a context to every entity of the phase, then collects the
 * answers. Entities work concurrently: all messages are sent first,
 * all answers are collected afterwards.
 */
static int run_phase(int phase, char role)
{
    Msg m;
    for (int i = 0; i < g_ent_count; ++i)
    {
        EntConn *e = &g_ent[i];
        if (e->role != role)
            continue;
        fill_ctx(&m, phase, e);
        if (write_full(e->rep_fd, &m, sizeof(m)) != 0)
            return -1;
        sem_post(e->ready);
    }
    for (int i = 0; i < g_ent_count; ++i)
    {
        EntConn *e = &g_ent[i];
        if (e->role != role)
            continue;
        if (wait_done() != 0)
            return -1;
        if (read_full(e->req_fd, &m, sizeof(m)) != 0)
            return -1;
        if (m.phase != phase)
            return -1;

        if (phase == PH_MOVE && role == 't')
        {
            int slot = e->slot;
            Tank *t = &g_st->tanks[slot];
            if (t->alive)
            {
                int nx = t->x + m.ax;
                int ny = t->y + m.ay;
                int ok = (m.ax == 0 && m.ay == 0) ||
                         (field_is_passable(&g_st->field, nx, ny) &&
                          !cell_occupied(g_st, nx, ny, t->id));
                if (ok)
                    tank_apply_step(g_st, slot, m.ax, m.ay);
                else
                    io_printf("[COORD  ] шаг танка #%d в (%d,%d) отклонён: "
                              "клетка недоступна — танк остаётся\n",
                              t->id, nx, ny);
            }
        }
        else if (phase == PH_OBSERVE)
        {
            int gslot = e->slot - g_st->tank_count;
            gun_observe_update(g_st, gslot,
                               m.gun.has_target, m.gun.target_id,
                               m.gun.last_x, m.gun.last_y,
                               m.gun.prev_x, m.gun.prev_y);
        }
        else if (phase == PH_FIRE)
        {
            int gslot = e->slot - g_st->tank_count;
            if (g_st->tick >= g_st->guns[gslot].next_fire)
                gun_apply_fire(g_st, gslot, m.ax, m.ay);
        }
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

    g_st = (SharedState *)calloc(1, sizeof(SharedState));
    if (!g_st || state_build(g_st, &cfg) != STATUS_OK)
    {
        io_printf("Ошибка построения начального состояния\n");
        free(g_st);
        log_close();
        return 1;
    }

    /* Semaphores and channels */
    if (sem_open_named(SEM_PRINT, 1, &g_print) != 0 ||
        sem_open_named(SEM_DONE, 0, &g_done) != 0 ||
        sem_open_named(SEM_ADMIN, 0, &g_admin) != 0)
    {
        cleanup_all();
        log_close();
        return 1;
    }
    io_set_lock(g_print);

    for (int i = 0; i < g_st->tank_count; ++i)
    {
        EntConn *e = &g_ent[g_ent_count];
        e->role = 't';
        e->id = i + 1;
        e->slot = g_ent_count;
        e->req_fd = e->rep_fd = -1;
        if (mk_chan("tank", e->id) != 0 || open_ready(e) != 0)
        {
            io_printf("Ошибка создания каналов\n");
            cleanup_all();
            log_close();
            return 1;
        }
        g_ent_count++;
    }
    for (int j = 0; j < g_st->gun_count; ++j)
    {
        EntConn *e = &g_ent[g_ent_count];
        e->role = 'g';
        e->id = j + 1;
        e->slot = g_ent_count;
        e->req_fd = e->rep_fd = -1;
        if (mk_chan("gun", e->id) != 0 || open_ready(e) != 0)
        {
            io_printf("Ошибка создания каналов\n");
            cleanup_all();
            log_close();
            return 1;
        }
        g_ent_count++;
    }

    /* Wait for the entities to attach */
    io_printf("Ожидание подключения %d процессов-сущностей "
              "(ent_tank_fifo/ent_gun_fifo)...\n",
              g_ent_count);
    for (int i = 0; i < g_ent_count; ++i)
    {
        if (attach_one(&g_ent[i]) != 0)
        {
            g_st->reason = sig_stop_requested() ? REASON_INTERRUPTED : REASON_ERROR;
            io_printf("Сбой подключения сущности (или прерывание)\n");
            break;
        }
        io_printf("Подключено: %d/%d\n", i + 1, g_ent_count);
    }

    Coord cview = {.st = g_st, .mutex = NULL, .done = NULL}; /* for banner/results */

    if (g_st->reason == REASON_RUNNING)
    {
        coord_banner(&cview);
        long long tick = 0;
        while (g_ent_count > 0)
        {
            if (sig_stop_requested())
            {
                g_st->reason = REASON_INTERRUPTED;
                break;
            }
            g_st->tick = tick;
            io_printf("\n=== Такт %lld ===\n", tick);
            if (g_st->verbose_field)
            {
                char buf[4096];
                field_print(&g_st->field, buf, sizeof(buf));
                io_printf("[FIELD ] состояние поля на такт %lld:\n%s", tick, buf);
            }
            if (g_st->delay_ms > 0)
            {
                struct timespec ts = {g_st->delay_ms / 1000,
                                      (long)(g_st->delay_ms % 1000) * 1000000L};
                nanosleep(&ts, NULL);
            }

            if (run_phase(PH_MOVE, 't') != 0 ||
                run_phase(PH_OBSERVE, 'g') != 0 ||
                run_phase(PH_FIRE, 'g') != 0)
            {
                g_st->reason = sig_stop_requested() ? REASON_INTERRUPTED : REASON_ERROR;
                break;
            }

            for (int i = 0; i < MAX_PROJECTILES; ++i)
                if (g_st->projectiles[i].alive &&
                    g_st->projectiles[i].impact_time <= g_st->tick)
                    proj_apply_impact(g_st, i);

            int r = sim_check_termination(g_st);
            if (r != REASON_RUNNING)
            {
                g_st->reason = r;
                break;
            }
            tick++;
        }
        if (g_st->reason == REASON_RUNNING)
        {
            int r = sim_check_termination(g_st);
            g_st->reason = (r == REASON_RUNNING) ? REASON_MAX_TICKS : r;
        }
        g_st->stats.ticks_simulated = g_st->tick;
        io_printf("\n=== Симуляция завершена: %s ===\n",
                  sim_reason_text(g_st->reason));
    }

    /* Ask the entities to finish: stop message + wake + SIGTERM survivors */
    {
        Msg m;
        memset(&m, 0, sizeof(m));
        m.phase = PH_STOP;
        m.pid = getpid();
        m.stop = 1;
        for (int i = 0; i < g_ent_count; ++i)
        {
            if (g_ent[i].rep_fd >= 0)
                write_full(g_ent[i].rep_fd, &m, sizeof(m));
            sem_post(g_ent[i].ready);
        }
        sleep(1);
        for (int i = 0; i < g_ent_count; ++i)
            if (g_ent[i].pid > 0 && pid_alive(g_ent[i].pid))
                kill(g_ent[i].pid, SIGTERM);
    }

    coord_print_results(&cview);

    cleanup_all();
    log_close();
    return 0;
}
