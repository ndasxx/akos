#ifndef COMMON_H
#define COMMON_H

/**
 * \file common.h
 * \brief General constants.
 *
 */

/** \brief The maximum number of tanks in the simulation. */
#define MAX_TANKS 32

/** \brief The maximum number of weapons in the simulation */
#define MAX_GUNS 16

/** \brief The maximum number of projectiles flying simultaneously. */
#define MAX_PROJECTILES 128

/**
 * \brief Return codes for functions
 */
typedef enum
{
    STATUS_OK = 0,  /**< Successful completion */
    STATUS_ERR = -1 /**< Execution error */
} status_t;

#endif /* COMMON_H */