/**
 * \file signals.c
 * \brief Implementation of signal handling for graceful termination.
 */

#include "signals.h"
#include <signal.h>
#include <string.h>

/* Stop flag: set by the signal handler, checked by the main loop. */
static volatile sig_atomic_t g_stop = 0;

static void on_stop(int signo)
{
    (void)signo;
    g_stop = 1;
}

int sig_install(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_stop;
    sigemptyset(&sa.sa_mask);

    /* No SA_RESTART: let blocking calls (nanosleep) be interrupted */
    sa.sa_flags = 0;

    if (sigaction(SIGINT, &sa, NULL) != 0)
        return -1;
    if (sigaction(SIGTERM, &sa, NULL) != 0)
        return -1;
    return 0;
}

int sig_stop_requested(void)
{
    return g_stop != 0;
}

void sig_reset(void)
{
    g_stop = 0;
}