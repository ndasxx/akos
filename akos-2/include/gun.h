#ifndef GUN_H
#define GUN_H

#include "common.h"
#include "rng.h"
#include "sync.h"

struct SimContext;

/**
 * \file gun.h
 * \brief Gun entity, observation, and firing strategies.
 */

/**
 * \brief Gun aiming strategies
 */
typedef enum
{
    GUN_RANDOM,   /**< Random point near last known position */
    GUN_AIM_LAST, /**< Exactly at last known position */
    GUN_PREDICT   /**< Last + (last - prev) */
} gun_strategy_t;

/**
 * \brief Returns the string name of an aiming strategy
 * \param s Strategy type
 * \return Strategy name (e.g., "random", "last", "predict")
 */
const char *gun_strategy_name(gun_strategy_t s);

/**
 * \brief Gun entity (every gun works in its own thread)
 */
typedef struct
{
    int id;
    int x, y;          /**< Gun position */
    int ammo;          /**< Remaining shells */
    int range;         /**< Observation range*/
    int reload;        /**< Ticks between firing attempts */
    int shot_delay;    /**< Ticks from firing to impact */
    int accuracy;      /**< 0..100 — probability of accurate aim */
    int blast_radius;  /**< 0 = single cell, k = "diamond" radius */
    int damage_direct; /**< Damage at impact cell */
    int damage_splash; /**< Damage within blast radius */
    gun_strategy_t strategy;

    /* Observation state */
    int has_target;
    int target_id;
    int last_x, last_y; /**< Last observed position */
    int prev_x, prev_y; /**< Previous position (for prediction) */

    /* Thread of the gun */
    long long next_fire; /**< Tick of the next firing attempt */
    Rng rng;             /**< Own random generator (seeded from the main seed) */
    Sema go;             /**< Posted by the field coordinator at the start of each phase */
    struct SimContext *ctx; /**< Simulation context (for the thread) */
    /* Statistics */
    long long shots_fired;
    long long hits_direct;
    long long hits_splash;
    long long misses;
} Oreshnik;

/**
 * \brief Observation phase of a gun
 * \param ctx   Simulation context
 * \param gun_id Gun ID
 * \return STATUS_OK on success, STATUS_ERR on error
 *
 * Scans for the nearest live tank within observation range, updates
 * last/prev positions.
 * Must be called with ctx->mutex locked.
 */
int gun_handle_observe(struct SimContext *ctx, int gun_id);

/**
 * \brief Firing phase of a gun
 * \param ctx    Simulation context
 * \param gun_id Gun ID
 * \return STATUS_OK on success, STATUS_ERR on error
 *
 * Checks ammo and target availability, chooses aim point, allocates
 * a projectile. The next attempt is planned at tick + reload.
 * Must be called with ctx->mutex locked.
 */
int gun_handle_fire(struct SimContext *ctx, int gun_id);

/**
 * \brief Chooses the aim point for the current strategy
 * \param g     Pointer to the gun
 * \param ctx   Simulation context
 * \param out_x Output: X coordinate of the aim point
 * \param out_y Output: Y coordinate of the aim point
 *
 * Applies the aiming strategy and inaccuracy (own generator of the gun),
 * then clamps to field bounds.
 */
void gun_choose_aim(Oreshnik *g, struct SimContext *ctx, int *out_x, int *out_y);

/**
 * \brief Thread of a gun
 * \param arg Pointer to the Oreshnik
 * \return NULL
 * Every tick works in two phases: observation and firing. Each phase starts
 * with the signal of the coordinator, runs under ctx->mutex and is reported
 * to the coordinator through ctx->done.
 */
void *gun_thread(void *arg);

#endif /* GUN_H */