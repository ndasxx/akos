#ifndef FIELD_H
#define FIELD_H

/**
 * \file field.h
 * \brief Rectangular grid for the simulation.
 *
 * Each cell has one of three states: empty, obstacle, or wreck.
 */

/**
 * \brief Cell states
 */
typedef enum
{
    CELL_EMPTY = 0,    /**< Empty passable cell */
    CELL_OBSTACLE = 1, /**< Static obstacle from configuration */
    CELL_WRECK = 2     /**< Wreckage of a destroyed tank */
} cell_t;

/**
 * \brief Rectangular field
 */
typedef struct
{
    int w, h;      /**< Grid dimensions */
    cell_t *cells; /**< Array of size w*h, index = y*w + x */
} Field;

/**
 * \brief Initializes the field
 * \param f Pointer to the field
 * \param w Width (must be > 0)
 * \param h Height (must be > 0)
 * \return 0 on success, -1 on error (invalid dimensions or allocation failure)
 *
 * All cells are initialized to CELL_EMPTY.
 */
int field_init(Field *f, int w, int h);

/**
 * \brief Frees the field
 * \param f Pointer to the field
 */
void field_free(Field *f);

/**
 * \brief Checks if coordinates are within the field bounds
 * \param f Pointer to the field
 * \param x X coordinate
 * \param y Y coordinate
 * \return Non-zero if in bounds, 0 otherwise
 */
int field_in_bounds(const Field *f, int x, int y);

/**
 * \brief Checks if a cell is passable
 * \param f Pointer to the field
 * \param x X coordinate
 * \param y Y coordinate
 * \return 1 if the cell is in bounds and is CELL_EMPTY, 0 otherwise
 *
 * Obstacles and wrecks are not passable.
 */
int field_is_passable(const Field *f, int x, int y);

/**
 * \brief Marks a cell as an obstacle
 * \param f Pointer to the field
 * \param x X coordinate
 * \param y Y coordinate
 *
 * Silently ignores coordinates outside the field bounds.
 */
void field_set_obstacle(Field *f, int x, int y);

/**
 * \brief Marks a cell as a wreck
 * \param f Pointer to the field
 * \param x X coordinate
 * \param y Y coordinate
 *
 * Silently ignores coordinates outside the field bounds.
 */
void field_set_wreck(Field *f, int x, int y);

/**
 * \brief Renders the field into a text buffer
 * \param f     Pointer to the field
 * \param buf   Output buffer
 * \param bufsz Buffer size
 *
 * Renders obstacles as '#', wrecks as 'X', empty cells as '.'.
 * Live tanks and guns are NOT rendered here — the caller overlays them.
 * The buffer is always NUL-terminated.
 */
void field_print(const Field *f, char *buf, int bufsz);

#endif /* FIELD_H */