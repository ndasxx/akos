#ifndef SHMUTIL_H
#define SHMUTIL_H

/**
 * \file shmutil.h
 * \brief Helpers for POSIX shared memory objects (shm_open + mmap).
 */

#include <sys/mman.h>
#include <sys/types.h>
#include <stddef.h>

/**
 * \brief Creates a new shared memory object of the given size and maps it
 * \param name  Object name, starting with '/'
 * \param size  Object size in bytes
 * \param out   Output: mapped address
 * \return File descriptor on success, -1 on error
 *
 * Fails if the object already exists (O_EXCL), so a stale object from a
 * previous crashed run cannot be used silently.
 */
int shm_create(const char *name, size_t size, void **out);

/**
 * \brief Opens an existing shared memory object and maps it
 * \param name  Object name, starting with '/'
 * \param size  Object size in bytes
 * \param out   Output: mapped address
 * \return 0 on success, -1 on error
 */
int shm_attach(const char *name, size_t size, void **out);

/**
 * \brief Opens an existing shared memory object, waiting for it to appear
 * \param name       Object name, starting with '/'
 * \param size       Object size in bytes
 * \param timeout_ms Total wait limit in milliseconds
 * \param out        Output: mapped address
 * \return 0 on success, -1 on error or timeout
 *
 * Used by the independently started entities (implementation C): the
 * coordinator may not have created the segment yet, so the open is
 * retried every 100 ms until it succeeds or the timeout expires.
 */
int shm_attach_wait(const char *name, size_t size, int timeout_ms, void **out);

/**
 * \brief Unmaps a shared memory segment and removes the object
 * \param addr Mapped address (may be NULL)
 * \param size Mapped size
 * \param name Object name
 */
void shm_destroy(void *addr, size_t size, const char *name);

#endif /* SHMUTIL_H */
