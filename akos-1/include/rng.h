#ifndef RNG_H
#define RNG_H

/**
 * \file rng.h
 * \brief wrapper over rand_r() for reproducible pseudo-random generation.
 *
 * Holds its own state so that the simulation is deterministic for a given seed
 * and independent of the global rand().
 */

/**
 * \brief Pseudo-random number generator state
 */
typedef struct
{
    unsigned int state; /**< Internal state passed to rand_r() */
} Rng;

/**
 * \brief Initialises the generator with a given seed
 * \param r Pointer to the generator
 * \param seed Seed value (same seed produces identical sequences)
 */
void rng_seed(Rng *r, unsigned int seed);

/**
 * \brief Returns an integer in [lo, hi]
 * \param r  Pointer to the generator
 * \param lo Lower bound (inclusive)
 * \param hi Upper bound (inclusive)
 * \return Random integer in [lo, hi]; bounds are swapped if lo > hi
 */
int rng_int(Rng *r, int lo, int hi);

/**
 * \brief Returns 1 with the given probability, 0 otherwise
 * \param r       Pointer to the generator
 * \param percent Probability in percents (0..100)
 * \return 1 if the event occurs, 0 otherwise
 */
int rng_chance(Rng *r, int percent);

#endif /* RNG_H */