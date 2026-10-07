#ifndef PROJECTILE_H
#define PROJECTILE_H

#include "common.h"

struct SimContext;

/**
 * \file projectile.h
 * \brief Projectile entity and impact handling.
 */

/**
 * \brief Projectile in flight
 */
typedef struct
{
    int id;                /**< Index in ctx->projectiles array */
    int gun_id;            /**< ID of the gun that fired it */
    int x, y;              /**< Target cell */
    int damage_direct;     /**< Damage at impact cell */
    int damage_splash;     /**< Damage within blast radius */
    int blast_radius;      /**< Splash radius */
    long long impact_time; /**< Tick when the projectile impacts */
    int alive;             /**< 0 = already exploded or not allocated */
} Projectile;

/**
 * \brief Allocates a free projectile slot
 * \param ctx Simulation context
 * \return Index of the allocated slot, or -1 if no free slots available
 */
int proj_alloc(struct SimContext *ctx);

/**
 * \brief Handles the EV_PROJECTILE_IMPACT event
 * \param ctx     Simulation context
 * \param proj_id Projectile ID
 * \return STATUS_OK on success, STATUS_ERR on error
 *
 * Applies direct damage at the target cell, splash
 * damage within blast_radius, updates hit/miss statistics for the gun.
 */
int proj_handle_impact(struct SimContext *ctx, int proj_id);

#endif /* PROJECTILE_H */