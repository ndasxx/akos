#ifndef TANK_H
#define TANK_H

#include "field.h"
#include "common.h"

struct SimContext;

/**
 * \file tank.h
 * \brief Tank entity and movement strategies.
 */

/**
 * \brief Tank movement strategies
 */
typedef enum
{
    TANK_STAY,        /**< Stays in place (stationary target) */
    TANK_RANDOM_WALK, /**< Random step or stay */
    TANK_STRAIGHT,    /**< Moves straight until blocked */
    TANK_PATROL       /**< Moves between two points */
} tank_strategy_t;

/**
 * \brief Returns the string name of a movement strategy
 * \param s Strategy type
 * \return Strategy name (e.g., "stay", "random_walk")
 */
const char *tank_strategy_name(tank_strategy_t s);

/**
 * \brief Tank entity
 */
typedef struct
{
    int id;                   /**< Display ID (1..N) */
    int x, y;                 /**< Current position */
    int hp;                   /**< Current health */
    int max_hp;               /**< Initial health */
    int alive;                /**< 0 = destroyed, cell contains wreck */
    tank_strategy_t strategy; /**< Movement strategy */

    /* For STRAIGHT/PATROL strategies */
    int dx, dy;               /**< Current direction */
    int patrol_x0, patrol_y0; /**< Patrol point A */
    int patrol_x1, patrol_y1; /**< Patrol point B */
    int patrol_leg;           /**< 0 = moving to (x0,y0), 1 = to (x1,y1) */

    /* Statistics */
    long long born_tick;      /**< Tick when created */
    long long moved_cells;    /**< Number of steps taken */
    long long shots_received; /**< Number of times hit */
} Tank;

/**
 * \brief Checks if a live tank occupies the given cell
 * \param tanks    Array of tanks
 * \param n        Number of tanks
 * \param x        X coordinate
 * \param y        Y coordinate
 * \param ignore_id Tank ID to ignore (typically the moving tank itself)
 * \return Non-zero if occupied, 0 otherwise
 */
int tank_is_occupied(const Tank *tanks, int n, int x, int y, int ignore_id);

/**
 * \brief Handles the EV_TANK_MOVE event for a tank
 * \param ctx     Simulation context
 * \param tank_id Tank ID
 * \return STATUS_OK on success, STATUS_ERR on error
 *
 * Moves the tank according to its strategy and schedules the next move event.
 */
int tank_handle_move(struct SimContext *ctx, int tank_id);

/**
 * \brief Applies damage to a tank
 * \param ctx     Simulation context
 * \param tank_id Tank ID
 * \param dmg     Damage amount
 * \param cause   Cause string for logging ("direct"/"splash")
 * \return STATUS_OK on success, STATUS_ERR on error
 *
 * Reduces HP, fixes destruction if HP <= 0, marks the cell as a wreck.
 */
int tank_apply_damage(struct SimContext *ctx, int tank_id,
                      int dmg, const char *cause);

#endif /* TANK_H */