/**
 * \file server.c
 * \brief Simulation server: TCP coordinator (field + tick loop).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <poll.h>
#include <sys/socket.h>

#include "common.h"
#include "state.h"
#include "cfg.h"
#include "logic.h"
#include "io.h"
#include "log.h"
#include "stats.h"
#include "signals.h"
#include "field.h"
#include "proto.h"
#include "net.h"

/** \brief Capacity of the monitor table */
#define MAX_MON 32

/** \brief Capacity of the control table */
#define MAX_CTL 8

static SharedState *g_st = NULL; /**< Authoritative state, lives only here */
static int g_listen = -1;

static int g_tank_fd[MAX_TANKS]; /**< TCP socket per tank slot, -1 = free */
static pid_t g_tank_pid[MAX_TANKS];
static int g_gun_fd[MAX_GUNS]; /**< TCP socket per gun slot, -1 = free */
static pid_t g_gun_pid[MAX_GUNS];

static int g_mon_fd[MAX_MON]; /**< Monitor sockets */
static int g_mon_count = 0;

static int g_ctl_fd[MAX_CTL]; /**< Control sockets */
static char g_ctl_buf[MAX_CTL][512];
static size_t g_ctl_len[MAX_CTL];
static int g_ctl_count = 0;

static int g_port = DEFAULT_PORT;

/* ------------------------------------------------------------------ */
/* Small utilities                                                    */
/* ------------------------------------------------------------------ */

static int *slot_fd(char role, int id)
{
    if (role == ROLE_TANK && id >= 1 && id <= g_st->tank_count)
        return &g_tank_fd[id - 1];
    if (role == ROLE_GUN && id >= 1 && id <= g_st->gun_count)
        return &g_gun_fd[id - 1];
    return NULL;
}

static pid_t *slot_pid(char role, int id)
{
    if (role == ROLE_TANK && id >= 1 && id <= g_st->tank_count)
        return &g_tank_pid[id - 1];
    if (role == ROLE_GUN && id >= 1 && id <= g_st->gun_count)
        return &g_gun_pid[id - 1];
    return NULL;
}

static int entity_connected(char role, int id)
{
    int *fd = slot_fd(role, id);
    return fd && *fd >= 0;
}

static int attached_count(void)
{
    int n = 0;
    for (int i = 0; i < g_st->tank_count; ++i)
        n += (g_tank_fd[i] >= 0);
    for (int i = 0; i < g_st->gun_count; ++i)
        n += (g_gun_fd[i] >= 0);
    return n;
}

/* ------------------------------------------------------------------ */
/* Message builders                                                   */
/* ------------------------------------------------------------------ */

static void fill_ctx(Msg *m, int phase, char role, int id)
{
    memset(m, 0, sizeof(*m));
    m->phase = phase;
    m->id = id;
    m->pid = getpid();
    m->tick = g_st->tick;
    m->seed = g_st->seed;
    m->field = g_st->field;
    memcpy(m->tanks, g_st->tanks, sizeof(m->tanks));
    m->tank_count = g_st->tank_count;
    if (role == ROLE_GUN)
        m->gun = g_st->guns[id - 1];
}

/* Builds the full integrated snapshot sent to the monitors. */
static void fill_mon(Msg *m)
{
    memset(m, 0, sizeof(*m));
    m->phase = PH_MON;
    m->pid = getpid();
    m->tick = g_st->tick;
    m->seed = g_st->seed;
    m->field = g_st->field;
    memcpy(m->tanks, g_st->tanks, sizeof(m->tanks));
    m->tank_count = g_st->tank_count;
    memcpy(m->guns, g_st->guns, sizeof(m->guns));
    m->gun_count = g_st->gun_count;
    memcpy(m->projectiles, g_st->projectiles, sizeof(m->projectiles));
    m->stats = g_st->stats;
    m->reason = g_st->reason;
}

/* ------------------------------------------------------------------ */
/* Monitors                                                           */
/* ------------------------------------------------------------------ */

static void mon_push(void)
{
    if (g_mon_count == 0)
        return;
    Msg m;
    fill_mon(&m);
    for (int i = 0; i < g_mon_count;)
    {
        if (net_write_full(g_mon_fd[i], &m, sizeof(m)) != 0)
        {
            io_printf("[СЕРВЕР ] монитор (fd %d) отключился\n", g_mon_fd[i]);
            close(g_mon_fd[i]);
            g_mon_fd[i] = g_mon_fd[--g_mon_count];
            continue;
        }
        ++i;
    }
}

/* ------------------------------------------------------------------ */
/* Entity connect/disconnect                                          */
/* ------------------------------------------------------------------ */

static void kick_entity(char role, int id, const char *note)
{
    int *fd = slot_fd(role, id);
    if (!fd || *fd < 0)
        return;
    Msg m;
    memset(&m, 0, sizeof(m));
    m.phase = PH_KICK;
    m.id = id;
    m.pid = getpid();
    snprintf(m.note, sizeof(m.note), "%s", note ? note : "отключён оператором");
    net_write_full(*fd, &m, sizeof(m));
    close(*fd);
    *fd = -1;
    pid_t *pid = slot_pid(role, id);
    if (pid)
        *pid = 0;
    io_printf("[СЕРВЕР ] %s #%d отключён (%s)\n",
              role == ROLE_TANK ? "танк" : "орудие", id, m.note);
}

static void detach_entity(char role, int id)
{
    int *fd = slot_fd(role, id);
    if (!fd || *fd < 0)
        return;
    close(*fd);
    *fd = -1;
    pid_t *pid = slot_pid(role, id);
    if (pid)
        *pid = 0;
    io_printf("[СЕРВЕР ] клиент %s #%d отключился — его фазы пропускаются\n",
              role == ROLE_TANK ? "tank" : "gun", id);
}

/* ------------------------------------------------------------------ */
/* Control protocol (text lines)                                      */
/* ------------------------------------------------------------------ */

static void ctl_reply(int fd, const char *text)
{
    net_write_full(fd, text, strlen(text));
}

static void cmd_list(int fd)
{
    char line[512];
    int off = snprintf(line, sizeof(line), "подключено: ");
    for (int i = 0; i < g_st->tank_count; ++i)
        off += snprintf(line + off, sizeof(line) - (size_t)off, "танк#%d:%s ",
                        i + 1, g_tank_fd[i] >= 0 ? "on" : "off");
    for (int i = 0; i < g_st->gun_count; ++i)
        off += snprintf(line + off, sizeof(line) - (size_t)off, "орудие#%d:%s ",
                        i + 1, g_gun_fd[i] >= 0 ? "on" : "off");
    snprintf(line + off, sizeof(line) - (size_t)off,
             "| мониторов: %d | контролей: %d | такт %lld\n",
             g_mon_count, g_ctl_count, g_st->tick);
    ctl_reply(fd, line);
}

static void kick_monitors(void)
{
    for (int i = 0; i < g_mon_count; ++i)
        close(g_mon_fd[i]);
    g_mon_count = 0;
}

static void handle_ctl_command(int fd, char *line)
{
    char arg1[32] = {0}, arg2[32] = {0};
    int n = sscanf(line, "%31s %31s", arg1, arg2);
    if (n <= 0)
        return;
    if (!strcmp(arg1, "list"))
        cmd_list(fd);
    else if (!strcmp(arg1, "kick") && n >= 2)
    {
        char role;
        int id = 0;
        if (!strcmp(arg2, "monitor"))
        {
            kick_monitors();
            io_printf("[СЕРВЕР ] все мониторы отключены оператором\n");
            ctl_reply(fd, "все мониторы отключены\n");
        }
        else if (sscanf(line, "%*s %31s %d", arg2, &id) >= 2 &&
                 (role = arg2[0]) && (role == 't' || role == 'g' || !strcmp(arg2, "tank") || !strcmp(arg2, "gun")))
        {
            char r = (arg2[0] == 't') ? ROLE_TANK : ROLE_GUN;
            if (!entity_connected(r, id))
                ctl_reply(fd, "клиент не подключён\n");
            else
            {
                kick_entity(r, id, "отключён оператором");
                char rep[128];
                snprintf(rep, sizeof(rep), "клиент %s #%d отключён\n",
                         r == ROLE_TANK ? "tank" : "gun", id);
                ctl_reply(fd, rep);
            }
        }
        else
            ctl_reply(fd, "использование: kick tank|gun N | kick monitor\n");
    }
    else if (!strcmp(arg1, "stop"))
    {
        ctl_reply(fd, "сервер завершает работу\n");
        io_printf("[СЕРВЕР ] команда stop от оператора\n");
        g_st->reason = REASON_INTERRUPTED;
        g_st->stop = 1;
    }
    else if (!strcmp(arg1, "help"))
        ctl_reply(fd,
                  "команды:\n"
                  "  list                — кто подключён\n"
                  "  kick tank N         — отключить танк #N\n"
                  "  kick gun N          — отключить орудие #N\n"
                  "  kick monitor        — отключить все мониторы\n"
                  "  stop                — завершить работу сервера\n"
                  "  help                — эта справка\n");
    else
        ctl_reply(fd, "неизвестная команда (help — справка)\n");
}

static void service_control(int idx)
{
    int fd = g_ctl_fd[idx];
    char *buf = g_ctl_buf[idx];
    ssize_t r = recv(fd, buf + g_ctl_len[idx],
                     sizeof(g_ctl_buf[0]) - 1 - g_ctl_len[idx], 0);
    if (r <= 0)
    {
        io_printf("[СЕРВЕР ] контроль-клиент (fd %d) отключился\n", fd);
        close(fd);
        g_ctl_fd[idx] = g_ctl_fd[--g_ctl_count];
        g_ctl_len[g_ctl_count] = 0;
        return;
    }
    g_ctl_len[idx] += (size_t)r;
    buf[g_ctl_len[idx]] = 0;

    char *start = buf;
    char *nl;
    while ((nl = strchr(start, '\n')) != NULL)
    {
        *nl = 0;
        handle_ctl_command(fd, start);
        start = nl + 1;
    }
    size_t rest = strlen(start);
    memmove(buf, start, rest + 1);
    g_ctl_len[idx] = rest;
}

/* ------------------------------------------------------------------ */
/* Accepting new connections                                          */
/* ------------------------------------------------------------------ */

static void mon_drop(int idx)
{
    io_printf("[СЕРВЕР ] монитор (fd %d) отключился\n", g_mon_fd[idx]);
    close(g_mon_fd[idx]);
    g_mon_fd[idx] = g_mon_fd[--g_mon_count];
}

static void accept_one(void)
{
    int fd = accept(g_listen, NULL, NULL);
    if (fd < 0)
        return;

    char role = 0;
    if (net_read_full(fd, &role, 1) != 0)
    {
        close(fd);
        return;
    }

    if (role == ROLE_MONITOR)
    {
        if (g_mon_count >= MAX_MON)
        {
            close(fd);
            return;
        }
        g_mon_fd[g_mon_count++] = fd;
        io_printf("[СЕРВЕР ] подключён монитор (fd %d), всего мониторов: %d\n",
                  fd, g_mon_count);
        Msg m;
        fill_mon(&m);
        if (net_write_full(fd, &m, sizeof(m)) != 0)
            mon_drop(g_mon_count - 1);
        return;
    }

    if (role == ROLE_CONTROL)
    {
        if (g_ctl_count >= MAX_CTL)
        {
            close(fd);
            return;
        }
        g_ctl_fd[g_ctl_count] = fd;
        g_ctl_len[g_ctl_count] = 0;
        g_ctl_count++;
        io_printf("[СЕРВЕР ] подключён контроль-клиент (fd %d)\n", fd);
        ctl_reply(fd, "управление сервером (help — справка)\n");
        return;
    }

    if (role != ROLE_TANK && role != ROLE_GUN)
    {
        close(fd);
        return;
    }

    Msg m;
    if (net_read_full(fd, &m, sizeof(m)) != 0 || m.phase != PH_ATTACH)
    {
        close(fd);
        return;
    }
    int *slot = slot_fd(role, m.id);
    if (!slot)
    {
        Msg k;
        memset(&k, 0, sizeof(k));
        k.phase = PH_KICK;
        k.id = m.id;
        snprintf(k.note, sizeof(k.note),
                 "неверный id (сервер ждёт %s 1..%d)",
                 role == ROLE_TANK ? "танки" : "орудия",
                 role == ROLE_TANK ? g_st->tank_count : g_st->gun_count);
        net_write_full(fd, &k, sizeof(k));
        close(fd);
        io_printf("[СЕРВЕР ] отклонено подключение: %s #%d (%s)\n",
                  role == ROLE_TANK ? "танк" : "орудие", m.id, k.note);
        return;
    }
    if (*slot >= 0)
    {
        Msg k;
        memset(&k, 0, sizeof(k));
        k.phase = PH_KICK;
        k.id = m.id;
        snprintf(k.note, sizeof(k.note), "идентификатор уже занят");
        net_write_full(fd, &k, sizeof(k));
        close(fd);
        io_printf("[СЕРВЕР ] отклонено подключение: %s #%d (дубль)\n",
                  role == ROLE_TANK ? "танк" : "орудие", m.id);
        return;
    }

    *slot = fd;
    *slot_pid(role, m.id) = m.pid;
    int cnt = attached_count();
    int total = g_st->tank_count + g_st->gun_count;
    io_printf("[СЕРВЕР ] подключён %s #%d (pid %d) — %d/%d\n",
              role == ROLE_TANK ? "танк" : "орудие", m.id, (int)m.pid,
              cnt, total);
    if (cnt == total)
        io_printf("[СЕРВЕР ] все сущности подключены, запуск тактов\n");

    memset(&m, 0, sizeof(m));
    m.phase = PH_WELCOME;
    m.pid = getpid();
    m.seed = g_st->seed;
    m.tick = g_st->tick;
    if (net_write_full(fd, &m, sizeof(m)) != 0)
        detach_entity(role, m.id);
}

/* ------------------------------------------------------------------ */
/* Asynchronous service: accepts, control lines, monitor EOF          */
/* ------------------------------------------------------------------ */

struct PollSet
{
    struct pollfd pfds[64];
    int idx[64];
    int n;
};

static void service_passive(int timeout_ms)
{
    struct pollfd pf[2 + MAX_MON + MAX_CTL];
    int kind[64], kidx[64];
    int n = 0;

    pf[n].fd = g_listen;
    pf[n].events = POLLIN;
    kind[n] = 0;
    kidx[n] = 0;
    n++;
    for (int i = 0; i < g_mon_count; ++i)
    {
        pf[n].fd = g_mon_fd[i];
        pf[n].events = POLLIN;
        kind[n] = 1;
        kidx[n] = i;
        n++;
    }
    for (int i = 0; i < g_ctl_count; ++i)
    {
        pf[n].fd = g_ctl_fd[i];
        pf[n].events = POLLIN;
        kind[n] = 2;
        kidx[n] = i;
        n++;
    }

    int rc = poll(pf, n, timeout_ms);
    if (rc <= 0)
        return;
    for (int i = 0; i < n; ++i)
    {
        if (!(pf[i].revents & (POLLIN | POLLERR | POLLHUP)))
            continue;
        if (kind[i] == 0)
            accept_one();
        else if (kind[i] == 1)
        {
            char b;
            ssize_t r = recv(pf[i].fd, &b, 1, MSG_PEEK);
            if (r <= 0)
                mon_drop(kidx[i]);
        }
        else
            service_control(kidx[i]);
    }
}

/* ------------------------------------------------------------------ */
/* Tick machine                                                       */
/* ------------------------------------------------------------------ */

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

static void srv_delay(int ms)
{
    if (ms <= 0)
        return;
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

static int run_phase(int phase, char role)
{
    int *fds;
    int count;
    if (role == ROLE_TANK)
    {
        fds = g_tank_fd;
        count = g_st->tank_count;
    }
    else
    {
        fds = g_gun_fd;
        count = g_st->gun_count;
    }

    Msg m;
    for (int i = 0; i < count; ++i)
    {
        if (fds[i] < 0)
            continue;
        fill_ctx(&m, phase, role, i + 1);
        if (net_write_full(fds[i], &m, sizeof(m)) != 0)
            detach_entity(role, i + 1);
    }

    char replied[64];
    memset(replied, 0, sizeof(replied));
    int pending = 0;
    for (int i = 0; i < count; ++i)
        if (fds[i] >= 0)
            pending++;

    long long waited_ms = 0;
    while (pending > 0 && !sig_stop_requested() && !g_st->stop)
    {
        struct pollfd pf[64];
        int ids[64];
        int n = 0;
        for (int i = 0; i < count; ++i)
        {
            if (fds[i] >= 0 && !replied[i])
            {
                pf[n].fd = fds[i];
                pf[n].events = POLLIN;
                ids[n] = i;
                n++;
            }
        }
        pf[n].fd = g_listen;
        pf[n].events = POLLIN;
        ids[n] = -1;
        n++;
        for (int c = 0; c < g_ctl_count; ++c)
        {
            pf[n].fd = g_ctl_fd[c];
            pf[n].events = POLLIN;
            ids[n] = -2 - c;
            n++;
        }

        int rc = poll(pf, n, 1000);
        if (rc < 0)
        {
            if (errno == EINTR)
                continue;
            return -1;
        }
        waited_ms += 1000;
        for (int i = 0; i < n; ++i)
        {
            if (!(pf[i].revents & (POLLIN | POLLERR | POLLHUP | POLLNVAL)))
                continue;
            if (ids[i] == -1)
            {
                accept_one();
                continue;
            }
            if (ids[i] <= -2)
            {
                service_control(-2 - ids[i]);
                continue;
            }

            int id = ids[i] + 1;
            if (net_read_full(fds[ids[i]], &m, sizeof(m)) != 0 ||
                m.phase != phase)
            {
                detach_entity(role, id);
                replied[ids[i]] = 1;
                pending--;
                continue;
            }
            replied[ids[i]] = 1;
            pending--;

            if (phase == PH_MOVE && role == ROLE_TANK)
            {
                Tank *t = &g_st->tanks[id - 1];
                if (t->alive)
                {
                    int nx = t->x + m.ax;
                    int ny = t->y + m.ay;
                    int ok = (m.ax == 0 && m.ay == 0) ||
                             (field_is_passable(&g_st->field, nx, ny) &&
                              !cell_occupied(g_st, nx, ny, t->id));
                    if (ok)
                        tank_apply_step(g_st, id - 1, m.ax, m.ay);
                    else
                        io_printf("[СЕРВЕР ] шаг танка #%d в (%d,%d) отклонён: "
                                  "клетка недоступна — танк остаётся\n",
                                  t->id, nx, ny);
                }
            }
            else if (phase == PH_OBSERVE)
            {
                gun_observe_update(g_st, id - 1,
                                   m.gun.has_target, m.gun.target_id,
                                   m.gun.last_x, m.gun.last_y,
                                   m.gun.prev_x, m.gun.prev_y);
            }
            else if (phase == PH_FIRE)
            {
                if (g_st->tick >= g_st->guns[id - 1].next_fire)
                    gun_apply_fire(g_st, id - 1, m.ax, m.ay);
            }
        }
        if (rc == 0 && waited_ms >= 5000 && pending > 0)
        {
            for (int i = 0; i < count; ++i)
                if (fds[i] >= 0 && !replied[i])
                {
                    io_printf("[СЕРВЕР ] %s #%d не отвечает 5 с — "
                              "фаза продолжается без него\n",
                              role == ROLE_TANK ? "танк" : "орудие", i + 1);
                    replied[i] = 1;
                    pending--;
                }
        }
    }

    if (sig_stop_requested())
        g_st->reason = REASON_INTERRUPTED;
    return (g_st->reason == REASON_INTERRUPTED || g_st->stop) ? -1 : 0;
}

/* ------------------------------------------------------------------ */
/* Output helpers (banner and final report)                           */
/* ------------------------------------------------------------------ */

static void srv_banner(void)
{
    io_printf("=== Симуляция (TCP, порт %d) ===\n", g_port);
    io_printf("Поле: %dx%d, танков: %d, орудий: %d, max_ticks: %lld\n",
              g_st->field.w, g_st->field.h,
              g_st->tank_count, g_st->gun_count, g_st->max_ticks);
}

static void srv_results(void)
{
    stats_print(&g_st->stats);

    io_printf("\nОрудия:\n");
    for (int i = 0; i < g_st->gun_count; ++i)
    {
        const Oreshnik *g = &g_st->guns[i];
        io_printf("  #%d (%d,%d): выстрелов %lld, попаданий %lld, промахов %lld, остаток патронов %d\n",
                  g->id, g->x, g->y, g->shots_fired, g->hits_direct,
                  g->misses, g->ammo);
    }

    io_printf("Танки:\n");
    for (int i = 0; i < g_st->tank_count; ++i)
    {
        const Tank *t = &g_st->tanks[i];
        io_printf("  #%d: %s, hp %d/%d, шагов %lld, попаданий по нему %lld\n",
                  t->id, t->alive ? "ВЫЖИЛ" : "УНИЧТОЖЕН",
                  t->hp, t->max_hp, t->moved_cells, t->shots_received);
    }

    io_printf("\nПричина завершения: %s\n", sim_reason_text(g_st->reason));
}

/* ------------------------------------------------------------------ */
/* Shutdown: tell every client and close everything                   */
/* ------------------------------------------------------------------ */

static void shutdown_all(void)
{
    Msg m;
    memset(&m, 0, sizeof(m));
    m.phase = PH_STOP;
    m.pid = getpid();
    m.stop = 1;
    m.tick = g_st->tick;
    m.reason = g_st->reason;
    snprintf(m.note, sizeof(m.note), "%s", sim_reason_text(g_st->reason));

    for (int i = 0; i < g_st->tank_count; ++i)
        if (g_tank_fd[i] >= 0)
            net_write_full(g_tank_fd[i], &m, sizeof(m));
    for (int i = 0; i < g_st->gun_count; ++i)
        if (g_gun_fd[i] >= 0)
            net_write_full(g_gun_fd[i], &m, sizeof(m));
    for (int i = 0; i < g_mon_count; ++i)
        net_write_full(g_mon_fd[i], &m, sizeof(m));
    for (int i = 0; i < g_ctl_count; ++i)
        ctl_reply(g_ctl_fd[i], "сервер завершил работу\n");

    srv_delay(300);

    for (int i = 0; i < g_st->tank_count; ++i)
        if (g_tank_fd[i] >= 0)
            close(g_tank_fd[i]);
    for (int i = 0; i < g_st->gun_count; ++i)
        if (g_gun_fd[i] >= 0)
            close(g_gun_fd[i]);
    for (int i = 0; i < g_mon_count; ++i)
        close(g_mon_fd[i]);
    for (int i = 0; i < g_ctl_count; ++i)
        close(g_ctl_fd[i]);
    g_mon_count = 0;
    g_ctl_count = 0;
    if (g_listen >= 0)
    {
        close(g_listen);
        g_listen = -1;
    }
}

/* ------------------------------------------------------------------ */
/* main                                                               */
/* ------------------------------------------------------------------ */

static int find_port(int argc, char **argv)
{
    for (int i = 1; i + 1 < argc; ++i)
        if (!strcmp(argv[i], "--port"))
            return atoi(argv[i + 1]);
    return DEFAULT_PORT;
}

int main(int argc, char **argv)
{
    io_init(STDOUT_FILENO);
    net_ignore_sigpipe();
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
        io_printf("  --port N                 TCP-порт сервера (7777)\n");
        return 0;
    }
    if (rc != 0)
        return 1;
    g_port = find_port(argc, argv);
    if (g_port <= 0 || g_port > 65535)
    {
        io_printf("Некорректный порт: %d\n", g_port);
        return 1;
    }

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

    for (int i = 0; i < MAX_TANKS; ++i)
        g_tank_fd[i] = -1;
    for (int i = 0; i < MAX_GUNS; ++i)
        g_gun_fd[i] = -1;

    g_listen = net_listen(g_port);
    if (g_listen < 0)
    {
        io_printf("Не удалось занять порт %d (уже используется?)\n", g_port);
        free(g_st);
        log_close();
        return 1;
    }

    srv_banner();

    int expected = g_st->tank_count + g_st->gun_count;
    io_printf("Ожидание подключения %d клиентов-сущностей "
              "(tank_client/gun_client)...\n",
              expected);
    while (attached_count() < expected && !sig_stop_requested())
        service_passive(1000);

    if (sig_stop_requested())
        g_st->reason = REASON_INTERRUPTED;

    if (g_st->reason == REASON_RUNNING)
    {
        long long tick = 0;
        while (g_st->tank_count + g_st->gun_count > 0)
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
            srv_delay(g_st->delay_ms);

            if (run_phase(PH_MOVE, ROLE_TANK) != 0 ||
                run_phase(PH_OBSERVE, ROLE_GUN) != 0 ||
                run_phase(PH_FIRE, ROLE_GUN) != 0)
                break;

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

            mon_push();
            service_passive(0);
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
        mon_push();
    }

    shutdown_all();
    srv_results();

    free(g_st);
    g_st = NULL;
    log_close();
    return 0;
}
