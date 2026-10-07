/**
 * \file rng.c
 * \brief Implementation of the reproducible PRNG wrapper.
 */

#include "rng.h"
#include <stdlib.h>

void rng_seed(Rng *r, unsigned int seed)
{
    r->state = seed;
}

int rng_int(Rng *r, int lo, int hi)
{
    /* Swap bounds if inverted */
    if (hi < lo)
    {
        int t = lo;
        lo = hi;
        hi = t;
    }

    unsigned int span = (unsigned int)(hi - lo + 1);
    return lo + (int)(rand_r(&r->state) % span);
}

int rng_chance(Rng *r, int percent)
{
    if (percent <= 0)
        return 0;
    if (percent >= 100)
        return 1;

    return (int)(rand_r(&r->state) % 100u) < percent;
}