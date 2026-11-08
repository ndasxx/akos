/**
 * \file semutil.c
 * \brief Implementation of the POSIX semaphore helpers.
 */

#include "semutil.h"
#include <fcntl.h>
#include <time.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int sem_open_named(const char *name, unsigned int init, sem_t **out)
{
    sem_t *s = sem_open(name, O_CREAT, 0666, init);
    if (s == SEM_FAILED)
    {
        perror("sem_open");
        return -1;
    }
    *out = s;
    return 0;
}

void sleep_ms(int ms)
{
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

sem_t *sem_open_wait(const char *name, int timeout_ms)
{
    int waited = 0;
    for (;;)
    {
        sem_t *s = sem_open(name, 0);
        if (s != SEM_FAILED)
            return s;
        if (waited >= timeout_ms)
            return SEM_FAILED;
        sleep_ms(100);
        waited += 100;
    }
}

int sem_wait_intr(sem_t *s)
{
    while (sem_wait(s) == -1)
    {
        if (errno != EINTR)
            return -1;
    }
    return 0;
}

int sem_timedwait_sec(sem_t *s, int sec)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += sec;

    for (;;)
    {
        if (sem_timedwait(s, &ts) == 0)
            return 0;
        if (errno == ETIMEDOUT)
            return 1;
        if (errno == EINTR)
            continue;
        return -1;
    }
}

int pid_alive(pid_t pid)
{
    if (pid <= 0)
        return 0;
    return kill(pid, 0) == 0;
}

void sem_name_entity(char *buf, size_t bufsz, const char *prefix, int id)
{
    snprintf(buf, bufsz, "%s_%d", prefix, id);
}
