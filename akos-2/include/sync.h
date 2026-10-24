#ifndef SYNC_H
#define SYNC_H

#include <pthread.h>
#ifndef COND
#include <semaphore.h>
#endif

/**
 * \file sync.h
 * \brief Counting semaphore used by the threads of the simulation.
 *
 * Default build: Sema is a POSIX semaphore (sem_t).
 * Build with -DCOND (make SYNC=cond): the same interface is implemented
 * with a mutex and a condition variable.
 */

#ifdef COND
/**
 * \brief Semaphore replacement: mutex + condition variable + counter
 */
typedef struct
{
    pthread_mutex_t mutex; /**< Protects count */
    pthread_cond_t cond;   /**< Signalled by sema_post */
    int count;             /**< Current value */
} Sema;
#else
/**
 * \brief POSIX semaphore
 */
typedef struct
{
    sem_t sem; /**< Underlying semaphore */
} Sema;
#endif

/**
 * \brief Initializes the semaphore
 * \param s     Pointer to the semaphore
 * \param value Initial value
 * \return 0 on success, -1 on error
 */
int sema_init(Sema *s, int value);

/**
 * \brief Destroys the semaphore
 * \param s Pointer to the semaphore
 */
void sema_destroy(Sema *s);

/**
 * \brief Increments the semaphore and wakes one waiting thread
 * \param s Pointer to the semaphore
 */
void sema_post(Sema *s);

/**
 * \brief Decrements the semaphore, waits while its value is zero
 * \param s Pointer to the semaphore
 * \return 0 on success, -1 on error
 */
int sema_wait(Sema *s);

#endif /* SYNC_H */
