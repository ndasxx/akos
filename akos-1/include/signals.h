#ifndef SIGNALS_H
#define SIGNALS_H

/**
 * \file signals.h
 * \brief Signal handling for graceful termination.
 *
 * Installs handlers for SIGINT and SIGTERM. The handler only sets a flag —
 * no async-unsafe operations are performed. The flag is checked from the
 * main loop via sig_stop_requested().
 */

/**
 * \brief Installs signal handlers for SIGINT and SIGTERM
 * \return 0 on success, -1 on error
 *
 * Note: SA_RESTART is not set, so blocking calls like nanosleep() will
 * be interrupted by the signal.
 */
int sig_install(void);

/**
 * \brief Checks if a stop signal has been received
 * \return Non-zero if SIGINT or SIGTERM was received, 0 otherwise
 */
int sig_stop_requested(void);

/**
 * \brief Resets the stop flag to zero
 *
 */
void sig_reset(void);

#endif /* SIGNALS_H */