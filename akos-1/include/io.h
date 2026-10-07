#ifndef IO_H
#define IO_H

/**
 * \file io.h
 * \brief Output layer over the write(2) system call.
 *
 * All events and messages are printed via io_printf(), which formats
 * the string into a buffer and writes it directly to the file descriptor.
 */

/**
 * \brief Sets the output file descriptor
 * \param fd File descriptor (typically STDOUT_FILENO = 1)
 * \return 0 on success
 */
int io_init(int fd);

/**
 * \brief Prints a formatted string to the output descriptor and mirror
 * \param fmt Format string (printf-style)
 * \param ... Variable arguments
 *
 * The string is formatted into an internal buffer  and written
 * to both the main descriptor and the mirror (if set).
 */
void io_printf(const char *fmt, ...);

/**
 * \brief Sets an additional descriptor for output mirroring
 * \param fd File descriptor for mirroring, or -1 to disable
 *
 * Every call to io_printf() will duplicate its output to this descriptor.
 * Typically used for logging to a file.
 */
void io_set_mirror(int fd);

#endif /* IO_H */