#ifndef CFG_H
#define CFG_H

/**
 * \file cfg.h
 * \brief Simulation configuration: CLI arguments, config file, interactive prompt.
 *
 * Priority: CLI arguments > config file > defaults. The configuration
 * runs entirely in the coordinator process before the entity processes
 * are spawned, so no settings cross process boundaries.
 */

#include "state.h"

/**
 * \brief Simulation configuration
 */
typedef struct
{
    int field_w;   /**< Field width */
    int field_h;   /**< Field height */
    int obstacles; /**< Number of random obstacles */

    int tanks;         /**< Number of tanks */
    int tank_hp;       /**< Tank durability */
    int tank_strategy; /**< tank_strategy_t for all tanks */

    int guns;          /**< Number of guns */
    int ammo;          /**< Shells per gun */
    int range;         /**< Observation range */
    int reload;        /**< Ticks between firing attempts */
    int shot_delay;    /**< Ticks from firing to impact */
    int accuracy;      /**< 0..100 */
    int blast_radius;  /**< Splash radius */
    int damage_direct; /**< Damage at impact cell */
    int damage_splash; /**< Damage within blast radius */
    int gun_strategy;  /**< gun_strategy_t for all guns */

    unsigned int seed;   /**< Base seed of the PRNG (0 = time-based) */
    long long max_ticks; /**< Tick limit (0 = unlimited) */
    int delay_ms;        /**< Delay between ticks, ms */
    int verbose_field;   /**< Dump field state every tick */
    char log_path[256];  /**< Log file path (empty = no log) */
    char cfg_file[256];  /**< Config file path (empty = none) */
} SimConfig;

/**
 * \brief Fills the config with default values
 * \param cfg Pointer to the configuration
 */
void cfg_defaults(SimConfig *cfg);

/**
 * \brief Loads configuration: CLI arguments, then --interactive prompt if requested
 * \param cfg  Pointer to the configuration (defaults must be set first)
 * \param argc Argument count from main()
 * \param argv Argument vector from main()
 * \return 0 on success, -1 on error, 1 if --help was requested
 */
int cfg_load(SimConfig *cfg, int argc, char **argv);

/**
 * \brief Prints usage information
 * \param progname Program name (argv[0])
 */
void cfg_usage(const char *progname);

/**
 * \brief Initializes the shared state from the configuration
 * \param st  Pointer to the shared state (zero-initialized memory)
 * \param cfg Pointer to the configuration
 * \return STATUS_OK on success, STATUS_ERR on error
 *
 * Places obstacles, tanks and guns on random free cells using a PRNG
 * seeded by cfg->seed, so the initial layout is reproducible.
 */
int state_build(SharedState *st, const SimConfig *cfg);

#endif /* CFG_H */
