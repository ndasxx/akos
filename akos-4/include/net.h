#ifndef NET_H
#define NET_H

/**
 * \file net.h
 * \brief Small TCP helpers shared by the server and the clients.
 *
 * The functions are thin wrappers over the socket API (socket/bind/
 * listen/accept/connect) with two conveniences required by the task:
 *  - net_connect_retry() patiently waits for the server to appear, so
 *    the client and the server can be started in any order;
 *  - net_write_full()/net_read_full() transfer a whole fixed-size
 *    message through the stream (TCP may split or merge bytes).
 *
 * SIGPIPE is ignored process-wide (net_ignore_sigpipe): writing to a
 * closed socket must return EPIPE, not kill the process.
 */

#include <stddef.h>

/**
 * \brief Ignores SIGPIPE (writing to a dead peer yields EPIPE instead)
 */
void net_ignore_sigpipe(void);

/**
 * \brief Connects to host:port, retrying for up to timeout_ms
 * \param host       Server host (name or IPv4 dotted string)
 * \param port       Server TCP port
 * \param timeout_ms Total wait budget in milliseconds
 * \return Connected socket, or -1 on failure
 *
 * The attempts repeat every 100 ms the server may simply not be
 * running yet when a client starts.
 */
int net_connect_retry(const char *host, int port, int timeout_ms);

/**
 * \brief Creates a listening socket on 0.0.0.0:port
 * \param port TCP port
 * \return Listening socket, or -1 on failure
 */
int net_listen(int port);

/**
 * \brief Writes exactly n bytes to a socket (handles partial writes)
 * \param fd Socket
 * \param buf Buffer
 * \param n Number of bytes
 * \return 0 on success, -1 on error (EPIPE, ECONNRESET, ...)
 */
int net_write_full(int fd, const void *buf, size_t n);

/**
 * \brief Reads exactly n bytes from a socket
 * \param fd Socket
 * \param buf Buffer
 * \param n Number of bytes
 * \return 0 on success, -1 on error or EOF (the peer has terminated)
 */
int net_read_full(int fd, void *buf, size_t n);

/**
 * \brief Waits until the socket becomes readable or the timeout expires
 * \param fd Socket
 * \param timeout_ms Timeout in milliseconds
 * \return 1 if readable, 0 on timeout, -1 on error
 */
int net_wait_readable(int fd, int timeout_ms);

/**
 * \brief Waits until the socket becomes writable or the timeout expires
 * \param fd Socket
 * \param timeout_ms Timeout in milliseconds
 * \return 1 if writable, 0 on timeout, -1 on error
 */
int net_wait_writable(int fd, int timeout_ms);

/**
 * \brief Suspends the current process for the given number of milliseconds
 * \param ms Milliseconds
 */
void sleep_ms(int ms);

#endif /* NET_H */
