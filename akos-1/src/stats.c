/**
 * \file stats.c
 * \brief Implementation of global simulation statistics.
 */

#include "stats.h"
#include "io.h"
#include <string.h>

/* Zero out all counters. */
void stats_init(SimStats *s)
{
    memset(s, 0, sizeof(*s));
}

/*
 * Print the final summary. KPI is calculated as (hits / total) * 100
 * with one decimal place.
 */
void stats_print(const SimStats *s)
{
    io_printf("\n=== ИТОГИ СИМУЛЯЦИИ ===\n");
    io_printf("Тактов смоделировано : %lld\n", s->ticks_simulated);
    io_printf("Выстрелов всего      : %lld\n", s->shots_total);
    io_printf("Попаданий            : %lld\n", s->shots_hit);
    io_printf("Промахов             : %lld\n", s->shots_missed);
    if (s->shots_total > 0)
    {
        /* Multiply by 1000 to get one decimal place: (hits * 1000) / total */
        long long kpi10 = (s->shots_hit * 1000) / s->shots_total;
        io_printf("KPI                  : %lld.%lld%%\n",
                  kpi10 / 10, kpi10 % 10);
    }
    io_printf("Уничтожено танков    : %lld\n", s->tanks_destroyed);
}