#ifndef SEMUTIL_H
#define SEMUTIL_H

/**
 * \file semutil.h
 * \brief Helpers over POSIX semaphores used by all implementations.
 *
 * Two flavours coexist in the task:
 *  - implementations A, C, D use named semaphores opened with sem_open();
 *  - implementation B places unnamed semaphores directly into the shared
 *    memory segment and initializes them with sem_init(pshared = 1).
 * These helpers cover the named flavour and the common wait loops.
 */

#include <semaphore.h>
#include <sys/types.h>

/**
 * \brief Opens (creating if needed) a named semaphore
 * \param name  Semaphore name, starting with '/'
 * \param init  Initial value used when the semaphore is created
 * \param out   Output: pointer to the opened semaphore
 * \return 0 on success, -1 on error
 */
int sem_open_named(const char *name, unsigned int init, sem_t **out);

/**
 * \brief Opens an EXISTING named semaphore, waiting for it to appear
 * \param name       Semaphore name, starting with '/'
 * \param timeout_ms Total wait limit in milliseconds
 * \return Pointer to the semaphore, or SEM_FAILED on timeout
 *
 * Used by the independently started entities (implementations C and D):
 * the coordinator may not have created its objects yet, so the open is
 * retried every 100 ms until it succeeds or the timeout expires.
 */
sem_t *sem_open_wait(const char *name, int timeout_ms);

/**
 * \brief Waits on a semaphore, retrying on EINTR
 * \param s Pointer to the semaphore
 * \return 0 on success, -1 on a real error
 */
int sem_wait_intr(sem_t *s);

/**
 * \brief Waits on a semaphore with a timeout, retrying on EINTR
 * \param s   Pointer to the semaphore
 * \param sec Timeout in seconds
 * \return 0 if the semaphore was acquired, 1 on timeout, -1 on error
 *
 * Use in the independent implementations: while waiting
 * for the coordinator the entity periodically re-checks the stop flag
 * and the liveness of the coordinator process.
 */
int sem_timedwait_sec(sem_t *s, int sec);

/**
 * \brief Checks whether a process with the given PID exists
 * \param pid Process ID
 * \return 1 if alive, 0 if dead or an error occurred
 */
int pid_alive(pid_t pid);

/**
 * \brief Sleeps for the given number of milliseconds
 * \param ms Sleep duration in milliseconds
 *
 * Wrapper over nanosleep: usleep() is not declared under the strict
 * POSIX feature test macros used by the project.
 */
void sleep_ms(int ms);

/**
 * \brief Builds the name of a per-entity "go" semaphore
 * \param buf     Output buffer
 * \param bufsz   Buffer size
 * \param prefix  Name prefix
 * \param id      Entity ID (1-based)
 */
void sem_name_entity(char *buf, size_t bufsz, const char *prefix, int id);

#endif /* SEMUTIL_H */
