#ifndef LOGIC_H
#define LOGIC_H

/**
 * \file logic.h
 * \brief Domain logic of the simulation: decisions and state updates.
 *
 * The layer is split into two parts:
 *  - pure decision functions (no printing, no mutation of SharedState)
 *    that can run inside any process;
 *  - apply functions that mutate SharedState, print events and are only
 *    called by whoever owns the state (entity processes under the mutex
 *    semaphore, or the coordinator).
 */

#include "state.h"
#include "rng.h"

/**
 * \brief Returns the string name of a movement strategy
 * \param s Strategy type
 * \return Strategy name (e.g., "stay", "random_walk")
 */
const char *tank_strategy_name(tank_strategy_t s);

/**
 * \brief Returns the string name of an aiming strategy
 * \param s Strategy type
 * \return Strategy name (e.g., "random", "last", "predict")
 */
const char *gun_strategy_name(gun_strategy_t s);

/**
 * \brief Returns human-readable text for a termination reason
 * \param reason Reason code
 * \return Reason description string
 */
const char *sim_reason_text(int reason);

/**
 * \brief Decides the next step of a tank (pure function)
 * \param field  Pointer to the field
 * \param tanks  Array of all tanks (for occupancy check)
 * \param n      Number of tanks
 * \param t      Pointer to the deciding tank
 * \param rng    Random generator of the tank process
 * \param out_dx Output: delta X (-1, 0, 1)
 * \param out_dy Output: delta Y (-1, 0, 1)
 * \return 1 if a step was chosen, 0 if the tank must stay
 *
 * Does not modify any state: the caller applies the chosen delta.
 */
int tank_decide_step(const ShmField *field, const Tank *tanks, int n,
                     Tank *t, Rng *rng, int *out_dx, int *out_dy);

/**
 * \brief Applies a decided move to the state and prints the event
 * \param st   Pointer to the shared state
 * \param slot Index of the tank in st->tanks
 * \param dx   Chosen delta X
 * \param dy   Chosen delta Y
 *
 * Must be called with the state mutex semaphore held.
 */
void tank_apply_step(SharedState *st, int slot, int dx, int dy);

/**
 * \brief Applies damage to a tank and prints the event
 * \param st      Pointer to the shared state
 * \param tank_id Tank ID (1..N)
 * \param dmg     Damage amount
 * \param cause   Cause string for logging ("direct"/"splash")
 *
 * Reduces HP, fixes destruction if HP <= 0, marks the cell as a wreck.
 * Must be called with the state mutex semaphore held.
 */
void tank_apply_damage(SharedState *st, int tank_id, int dmg, const char *cause);

/**
 * \brief Decides the observation result of a gun (pure function)
 * \param field      Pointer to the field
 * \param tanks      Array of all tanks
 * \param n          Number of tanks
 * \param gx         Gun position
 * \param gy         Gun position
 * \param range      Observation range
 * \param has_target Input/output: target presence flag
 * \param target_id  Input/output: observed tank ID
 * \param last_x     Input/output: last observed position
 * \param last_y     Input/output: last observed position
 * \param prev_x     Input/output: previous observed position
 * \param prev_y     Input/output: previous observed position
 *
 * Scans for the nearest live tank within Manhattan range and updates
 * the observation state accordingly.
 */
void gun_decide_observe(const ShmField *field, const Tank *tanks, int n,
                        int gx, int gy, int range,
                        int *has_target, int *target_id,
                        int *last_x, int *last_y,
                        int *prev_x, int *prev_y);

/**
 * \brief Chooses the aim point for the given strategy (pure function)
 * \param strategy Gun strategy
 * \param last_x   Last observed position
 * \param last_y   Last observed position
 * \param prev_x   Previous observed position
 * \param prev_y   Previous observed position
 * \param accuracy 0..100 — probability of accurate aim
 * \param w        Field dimensions (for clamping)
 * \param h        Field dimensions (for clamping)
 * \param rng      Random generator of the gun process
 * \param out_x    Output: X of the aim point
 * \param out_y    Output: Y of the aim point
 */
void gun_decide_aim(gun_strategy_t strategy,
                    int last_x, int last_y, int prev_x, int prev_y,
                    int accuracy, int w, int h, Rng *rng,
                    int *out_x, int *out_y);

/**
 * \brief Stores the new observation state of a gun and prints the events
 * \param st             Pointer to the shared state
 * \param slot           Index of the gun in st->guns
 * \param new_has_target New target presence flag
 * \param new_target_id  New observed tank ID
 * \param new_last_x     New last observed position
 * \param new_last_y     New last observed position
 * \param new_prev_x     New previous observed position
 * \param new_prev_y     New previous observed position
 *
 *
 * Must be called with the state mutex semaphore held (or by the sole
 * owner of the state - the FIFO coordinator).
 */
void gun_observe_update(SharedState *st, int slot,
                        int new_has_target, int new_target_id,
                        int new_last_x, int new_last_y,
                        int new_prev_x, int new_prev_y);

/**
 * \brief Applies the observation of a gun and prints the events
 * \param st   Pointer to the shared state
 * \param slot Index of the gun in st->guns
 *
 * Must be called with the state mutex semaphore held.
 */
void gun_apply_observe(SharedState *st, int slot);

/**
 * \brief Applies a shot at the aim point and prints the events
 * \param st   Pointer to the shared state
 * \param slot Index of the gun in st->guns
 * \param ax   Aim X
 * \param ay   Aim Y
 * \return 1 if a projectile was launched, 0 otherwise
 *
 * Checks ammo and target availability, allocates a projectile,
 * schedules the impact at tick + shot_delay.
 * Must be called with the state mutex semaphore held.
 */
int gun_apply_fire(SharedState *st, int slot, int ax, int ay);

/**
 * \brief Resolves the impact of a projectile and prints the events
 * \param st      Pointer to the shared state
 * \param proj_id Index in the projectiles array
 *
 * Applies direct damage at the target cell, splash damage within the
 * blast radius, updates hit/miss statistics.
 * Must be called with the state mutex semaphore held.
 */
void proj_apply_impact(SharedState *st, int proj_id);

/**
 * \brief Checks termination conditions
 * \param st Pointer to the shared state
 * \return Termination reason code, or REASON_RUNNING if still active
 */
int sim_check_termination(SharedState *st);

/**
 * \brief Returns the index of a tank by its ID
 * \param st      Pointer to the shared state
 * \param tank_id Tank ID
 * \return Index in st->tanks, or -1
 */
int tank_slot_by_id(const SharedState *st, int tank_id);

/**
 * \brief Returns the index of a gun by its ID
 * \param st     Pointer to the shared state
 * \param gun_id Gun ID
 * \return Index in st->guns, or -1
 */
int gun_slot_by_id(const SharedState *st, int gun_id);

#endif /* LOGIC_H */
