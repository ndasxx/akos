/**
 * \file ent_tank_fifo.c
 * \brief Tank process of implementation D
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>

#include "msg.h"
#include "logic.h"
#include "rng.h"
#include "io.h"
#include "signals.h"
#include "semutil.h"

/** \brief Semaphore family prefix of implementation D */
#define NS_PREFIX "/akos3d"

int main(int argc, char **argv)
{
    io_init(STDOUT_FILENO);

    int id = 0;
    for (int i = 1; i + 1 < argc; ++i)
        if (!strcmp(argv[i], "--id"))
            id = atoi(argv[i + 1]);
    if (id < 1)
    {
        fprintf(stderr, "Использование: %s --id N\n", argv[0]);
        return 1;
    }
    if (sig_install() != 0)
    {
        fprintf(stderr, "Не удалось установить обработчики сигналов\n");
        return 1;
    }

    char req_path[128], rep_path[128];
    fifo_path(req_path, sizeof(req_path), "req", "tank", id);
    fifo_path(rep_path, sizeof(rep_path), "rep", "tank", id);

    /* Create the channels (they may already exist) and attach */
    if (mkfifo(req_path, 0666) == -1 && errno != EEXIST)
    {
        perror("mkfifo");
        return 1;
    }
    if (mkfifo(rep_path, 0666) == -1 && errno != EEXIST)
    {
        perror("mkfifo");
        return 1;
    }

    int req_fd = -1;
    int w = 0;
    for (;;)
    {
        req_fd = open(req_path, O_WRONLY | O_NONBLOCK);
        if (req_fd >= 0)
            break;
        if (errno != ENXIO || w >= 10000 || sig_stop_requested())
        {
            perror("open req");
            return 1;
        }
        sleep_ms(100);
        w += 100;
    }
    /* Back to the ordinary blocking mode for the phase traffic. */
    fcntl(req_fd, F_SETFL, fcntl(req_fd, F_GETFL) & ~O_NONBLOCK);

    Msg m;
    memset(&m, 0, sizeof(m));
    m.phase = PH_ATTACH;
    m.id = id;
    m.pid = getpid();
    if (write_full(req_fd, &m, sizeof(m)) != 0)
        return 1;

    char name[64];
    sem_t *admin, *done, *ready;
    admin = sem_open_wait(NS_PREFIX "_admin", 10000);
    done = sem_open_wait(NS_PREFIX "_done", 10000);
    if (admin == SEM_FAILED || done == SEM_FAILED)
    {
        fprintf(stderr, "Нет семафоров — coord_fifo не запущен?\n");
        return 1;
    }
    sem_name_entity(name, sizeof(name), NS_PREFIX "_ready_tank", id);
    ready = sem_open_wait(name, 10000);
    if (ready == SEM_FAILED)
    {
        fprintf(stderr, "Нет семафора %s — координатор не запущен?\n", name);
        return 1;
    }
    sem_post(admin);

    /* Acknowledgement: coordinator PID + base seed */
    int rep_fd = open(rep_path, O_RDONLY);
    if (rep_fd < 0)
    {
        perror("open rep");
        return 1;
    }
    if (read_full(rep_fd, &m, sizeof(m)) != 0 || m.phase != PH_ATTACH)
    {
        fprintf(stderr, "Нет подтверждения от координатора\n");
        return 1;
    }
    pid_t coord_pid = m.pid;
    unsigned int seed = m.seed;

    Rng rng;
    rng_seed(&rng, seed ^ (0x9E3779B9u * (unsigned int)(id + 1)));

    for (;;)
    {
        int rc = sem_timedwait_sec(ready, 1);
        if (rc != 0)
        {
            if (rc < 0 || sig_stop_requested() || !pid_alive(coord_pid))
                break;
            continue; /* timeout: watchdog round */
        }
        if (read_full(rep_fd, &m, sizeof(m)) != 0)
            break; /* coordinator gone: EOF */
        if (m.stop || m.phase == PH_STOP)
            break;

        if (m.phase == PH_MOVE)
        {
            Tank self = m.tanks[id - 1];
            int dx, dy;
            tank_decide_step(&m.field, m.tanks, m.tank_count, &self, &rng,
                             &dx, &dy);
            memset(&m, 0, sizeof(m));
            m.phase = PH_MOVE;
            m.id = id;
            m.ax = dx;
            m.ay = dy;
            if (write_full(req_fd, &m, sizeof(m)) != 0)
                break;
            sem_post(done);
        }
    }

    sem_close(ready);
    sem_close(done);
    sem_close(admin);
    close(req_fd);
    close(rep_fd);
    return 0;
}
