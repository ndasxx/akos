#ifndef STATE_H
#define STATE_H

/**
 * \file state.h
 * \brief Shared simulation state placed into POSIX shared memory.
 *
 * All four implementations exchange data through a single
 * SharedState structure: the field, the tanks, the guns, the projectiles
 * and the aggregated statistics.
 */

#include <sys/types.h>
#include "common.h"

/**
 * \brief Cell states
 */
typedef enum
{
    CELL_EMPTY = 0,    /**< Empty passable cell */
    CELL_OBSTACLE = 1, /**< Static obstacle from configuration */
    CELL_WRECK = 2     /**< Wreckage of a destroyed tank */
} cell_t;

/**
 * \brief Rectangular field stored inline (no pointers, shared-memory safe)
 */
typedef struct
{
    int w, h;                /**< Grid dimensions */
    cell_t cells[MAX_CELLS]; /**< Index = y*w + x */
} ShmField;

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
 * \brief Tank entity.
 *
 * Every tank is a process. Its random generator and synchronization
 * primitives live in the process, not here.
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
 * \brief Gun aiming strategies
 */
typedef enum
{
    GUN_RANDOM,   /**< Random point near last known position */
    GUN_AIM_LAST, /**< Exactly at last known position */
    GUN_PREDICT   /**< Last + (last - prev) */
} gun_strategy_t;

/**
 * \brief Gun entity. Every gun is a process.
 */
typedef struct
{
    int id;
    int x, y;          /**< Gun position */
    int ammo;          /**< Remaining shells */
    int range;         /**< Observation range */
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

    /* Statistics */
    long long next_fire; /**< Tick of the next firing attempt */
    long long shots_fired;
    long long hits_direct;
    long long hits_splash;
    long long misses;
} Oreshnik;

/**
 * \brief Projectile in flight
 */
typedef struct
{
    int id;                /**< Index in the projectiles array */
    int gun_id;            /**< ID of the gun that fired it */
    int x, y;              /**< Target cell */
    int damage_direct;     /**< Damage at impact cell */
    int damage_splash;     /**< Damage within blast radius */
    int blast_radius;      /**< Splash radius */
    long long impact_time; /**< Tick when the projectile impacts */
    int alive;             /**< 0 = already exploded or not allocated */
} Projectile;

/**
 * \brief Aggregated statistics for the entire simulation
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
 * \brief Termination reason codes
 */
enum
{
    REASON_RUNNING = 0,       /**< Simulation is still running */
    REASON_ALL_DESTROYED = 1, /**< All tanks destroyed */
    REASON_NO_AMMO = 2,       /**< All guns out of ammo */
    REASON_MAX_TICKS = 3,     /**< Tick limit reached */
    REASON_INTERRUPTED = 4,   /**< Interrupted by user (SIGINT/SIGTERM) */
    REASON_ERROR = 5          /**< Execution error */
};

/**
 * \brief The complete simulation state shared by all processes.
 *
 * One instance of this structure lives in a POSIX shared memory object.
 * Processes map it with mmap() and protect every access with a binary
 * semaphore acting as a mutex.
 */
typedef struct
{
    ShmField field;                          /**< Battlefield */
    Tank tanks[MAX_TANKS];                   /**< Tank entities */
    int tank_count;                          /**< Number of active tanks */
    Oreshnik guns[MAX_GUNS];                 /**< Gun entities */
    int gun_count;                           /**< Number of active guns */
    Projectile projectiles[MAX_PROJECTILES]; /**< Projectile pool */
    SimStats stats;                          /**< Aggregated statistics */

    long long tick;      /**< Current model tick */
    long long max_ticks; /**< Tick limit (0 = unlimited) */
    int delay_ms;        /**< Delay between ticks, ms */
    int verbose_field;   /**< Print field state each tick */

    int stop;   /**< Set by the coordinator to ask entities to finish */
    int reason; /**< Termination reason code (see enum above) */

    unsigned int seed;               /**< Base seed of the reproducible PRNG */
    int attached;                    /**< Number of entities attached (independent impl.) */
    pid_t coord_pid;                 /**< PID of the coordinator process */
    pid_t entity_pids[MAX_ENTITIES]; /**< PIDs of attached entities */
} SharedState;

#endif /* STATE_H */
