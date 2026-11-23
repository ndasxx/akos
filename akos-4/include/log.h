#ifndef LOG_H
#define LOG_H

/**
 * \file log.h
 * \brief File-based logging via a file descriptor.
 *
 * The log file is opened with O_APPEND so that lines written by several
 * processes (after fork they share the descriptor) do not overwrite
 * each other.
 */

/**
 * \brief Opens a log file for writing
 * \param path Path to the log file
 * \return File descriptor on success, -1 on error
 */
int log_open(const char *path);

/**
 * \brief Flushes and closes the log file
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
