#ifndef STATS_H
#define STATS_H

/**
 * \file stats.h
 * \brief Global simulation statistics.
 */

/**
 * \brief Aggregated statistics for the entire simulation
 *
 * Updated at the points where corresponding events occur (firing, hits, etc.).
 * Printed and logged upon completion.
 */
typedef struct
{
    long long shots_total;     /**< Total number of shots fired */
    long long shots_hit;       /**< Shots that caused any damage */
    long long shots_missed;    /**< Complete misses */
    long long tanks_destroyed; /**< Number of tanks destroyed */
    long long ticks_simulated; /**< Number of ticks simulated */
} SimStats;

/**
 * \brief Initializes the statistics structure
 * \param s Pointer to the statistics structure
 *
 * Sets all counters to zero.
 */
void stats_init(SimStats *s);

/**
 * \brief Prints the final statistics to the output
 * \param s Pointer to the statistics structure
 *
 * Includes total shots, hits, misses, KPI (hit percentage with one decimal),
 * and number of tanks destroyed.
 */
void stats_print(const SimStats *s);

#endif /* STATS_H */