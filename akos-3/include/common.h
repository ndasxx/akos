#ifndef COMMON_H
#define COMMON_H

/**
 * \file common.h
 * \brief General constants
 */

/** \brief The maximum number of tanks in the simulation. */
#define MAX_TANKS 32

/** \brief The maximum number of guns in the simulation. */
#define MAX_GUNS 16

/** \brief The maximum number of projectiles flying simultaneously. */
#define MAX_PROJECTILES 128

/** \brief Maximum field width. */
#define MAX_FIELD_W 32

/** \brief Maximum field height. */
#define MAX_FIELD_H 32

/** \brief Maximum number of cells (kept in shared memory, so it is bounded). */
#define MAX_CELLS (MAX_FIELD_W * MAX_FIELD_H)

/** \brief Total number of entity slots (tanks first, then guns). */
#define MAX_ENTITIES (MAX_TANKS + MAX_GUNS)

/**
 * \brief Return codes for functions.
 */
typedef enum
{
    STATUS_OK = 0,  /**< Successful completion */
    STATUS_ERR = -1 /**< Execution error */
} status_t;

#endif /* COMMON_H */
