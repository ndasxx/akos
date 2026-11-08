#ifndef STATS_H
#define STATS_H

/**
 * \file stats.h
 * \brief Global simulation statistics.
 */

#include "state.h"

/**
 * \brief Initializes the statistics structure (all counters to zero)
 * \param s Pointer to the statistics structure
 */
void stats_init(SimStats *s);

/**
 * \brief Prints the final statistics to the output
 * \param s Pointer to the statistics structure
 */
void stats_print(const SimStats *s);

#endif /* STATS_H */
