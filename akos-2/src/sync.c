/**
 * \file sync.c
 * \brief Implementation of the semaphore (sem_t or mutex + condition variable).
 */
#include "sync.h"
#include <errno.h>

#ifndef COND

int sema_init(Sema *s, int value)
{
    return sem_init(&s->sem, 0, (unsigned int)value) == 0 ? 0 : -1;
}

void sema_destroy(Sema *s)
{
    sem_destroy(&s->sem);
}

void sema_post(Sema *s)
{
    sem_post(&s->sem);
}

/* A signal (Ctrl+C) can interrupt sem_wait: in that case wait again. */
int sema_wait(Sema *s)
{
    while (sem_wait(&s->sem) == -1)
    {
        if (errno != EINTR)
            return -1;
    }
    return 0;
}

#else

int sema_init(Sema *s, int value)
{
    s->count = value;
    if (pthread_mutex_init(&s->mutex, NULL) != 0)
        return -1;
    return pthread_cond_init(&s->cond, NULL) == 0 ? 0 : -1;
}

void sema_destroy(Sema *s)
{
    pthread_cond_destroy(&s->cond);
    pthread_mutex_destroy(&s->mutex);
}

void sema_post(Sema *s)
{
    pthread_mutex_lock(&s->mutex);
    s->count++;
    pthread_cond_signal(&s->cond);
    pthread_mutex_unlock(&s->mutex);
}

/* Sleep while the counter is zero, then take one unit. */
int sema_wait(Sema *s)
{
    pthread_mutex_lock(&s->mutex);
    while (s->count == 0)
        pthread_cond_wait(&s->cond, &s->mutex);
    s->count--;
    pthread_mutex_unlock(&s->mutex);
    return 0;
}

#endif
