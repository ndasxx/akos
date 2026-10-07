#ifndef CONFIG_H
#define CONFIG_H

#include "sim.h"

/**
 * \file config.h
 * \brief Configuration loading from CLI arguments, config file, and interactive input.
 */

/**
 * \brief Loads configuration into the simulation context
 * \param ctx  Simulation context to populate
 * \param argc Argument count from main()
 * \param argv Argument vector from main()
 * \return 0 on success, -1 on error
 *
 * Priority: CLI arguments > config file > defaults.
 * If --interactive is specified, prompts the user for all parameters.
 */
int config_load(SimContext *ctx, int argc, char **argv);

/**
 * \brief Prints usage information for --help
 * \param progname Program name (argv[0])
 */
void config_usage(const char *progname);

#endif /* CONFIG_H */