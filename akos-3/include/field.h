#ifndef FIELD_H
#define FIELD_H

/**
 * \file field.h
 * \brief Operations on the rectangular field stored in shared memory.
 *
 * Unlike the thread version the cells are stored inline in ShmField,
 * so the field can live inside a shared memory segment without pointers.
 */

#include "state.h"

/**
 * \brief Initializes the field with the given dimensions
 * \param f Pointer to the field
 * \param w Width (1..MAX_FIELD_W)
 * \param h Height (1..MAX_FIELD_H)
 * \return STATUS_OK on success, STATUS_ERR on invalid dimensions
 *
 * All cells are set to CELL_EMPTY.
 */
int field_init(ShmField *f, int w, int h);

/**
 * \brief Checks if coordinates are within the field bounds
 * \param f Pointer to the field
 * \param x X coordinate
 * \param y Y coordinate
 * \return Non-zero if in bounds, 0 otherwise
 */
int field_in_bounds(const ShmField *f, int x, int y);

/**
 * \brief Checks if a cell is passable
 * \param f Pointer to the field
 * \param x X coordinate
 * \param y Y coordinate
 * \return 1 if the cell is in bounds and is CELL_EMPTY, 0 otherwise
 */
int field_is_passable(const ShmField *f, int x, int y);

/**
 * \brief Marks a cell as an obstacle (silently ignores out-of-bounds)
 * \param f Pointer to the field
 * \param x X coordinate
 * \param y Y coordinate
 */
void field_set_obstacle(ShmField *f, int x, int y);

/**
 * \brief Marks a cell as a wreck (silently ignores out-of-bounds)
 * \param f Pointer to the field
 * \param x X coordinate
 * \param y Y coordinate
 */
void field_set_wreck(ShmField *f, int x, int y);

/**
 * \brief Renders the field into a text buffer
 * \param f     Pointer to the field
 * \param buf   Output buffer
 * \param bufsz Buffer size
 *
 * Renders obstacles as '#', wrecks as 'X', empty cells as '.'.
 * The buffer is NUL-terminated.
 */
void field_print(const ShmField *f, char *buf, int bufsz);

#endif /* FIELD_H */
