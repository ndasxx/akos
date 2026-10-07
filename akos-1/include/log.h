#ifndef LOG_H
#define LOG_H

/**
 * \file log.h
 * \brief File-based logging via a file descriptor.
 *
 * Works independently of io_printf(), so the log is preserved even when
 * stdout is redirected. The descriptor can be retrieved via log_fd() and
 * passed to io_set_mirror() to duplicate all output into the log.
 */

/**
 * \brief Opens a log file for writing
 * \param path Path to the log file
 * \return File descriptor on success, -1 on error
 *
 * Creates or truncates the file. Mode is 0644.
 */
int log_open(const char *path);

/**
 * \brief Flushes and closes the log file
 *
 * Calls fsync() before close() to ensure all data is written to disk.
 * Safe to call if the log is not open.
 */
void log_close(void);

/**
 * \brief Returns the current log file descriptor
 * \return File descriptor, or -1 if the log is not open
 */
int log_fd(void);

/**
 * \brief Writes a string to the log file
 * \param line String to write (must be NUL-terminated)
 */
void log_line(const char *line);

#endif /* LOG_H */