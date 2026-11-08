#ifndef IO_H
#define IO_H

/**
 * \file io.h
 * \brief Output layer over the write(2) system call.
 *
 * All events are printed via io_printf(), which formats the string into
 * a buffer and writes it directly to the file descriptor. Because several
 * processes write to the same descriptor, an optional binary semaphore
 * (living in shared memory or opened by name) serializes whole lines.
 */

#include <semaphore.h>

/**
 * \brief Sets the output file descriptor
 * \param fd File descriptor (typically STDOUT_FILENO = 1)
 * \return 0 on success
 */
int io_init(int fd);

/**
 * \brief Installs a semaphore that serializes output lines between processes
 * \param lock Pointer to an opened/mapped binary semaphore, or NULL
 *
 * With multiple processes writing to one terminal lines could interleave;
 * locking the formatting+write sequence keeps each line atomic.
 */
void io_set_lock(sem_t *lock);

/**
 * \brief Prints a formatted string to the output descriptor and mirror
 * \param fmt Format string (printf-style)
 * \param ... Variable arguments
 */
void io_printf(const char *fmt, ...);

/**
 * \brief Sets an additional descriptor for output mirroring
 * \param fd File descriptor for mirroring, or -1 to disable
 */
void io_set_mirror(int fd);

#endif /* IO_H */
