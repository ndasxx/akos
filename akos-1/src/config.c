/**
 * \file config.c
 * \brief Implementation of configuration loading and interactive setup.
 */

#include "config.h"
#include "io.h"
#include "rng.h"
#include "field.h"
#include "tank.h"
#include "gun.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>

/*
 * 1. Config file parsing
 */

/*
 * Find a key in the config file buffer. Format: "key  value..."
 * Lines starting with # are comments. Returns pointer to the value
 * or NULL if not found.
 */
static const char *cfg_find(const char *buf, const char *key)
{
    size_t klen = strlen(key);
    const char *p = buf;
    while (p && *p)
    {
        /* Skip leading whitespace */
        while (*p == ' ' || *p == '\t')
            ++p;
        /* Skip comments and empty lines */
        if (*p == '#' || *p == '\n')
        {
            while (*p && *p != '\n')
                ++p;
            continue;
        }
        if (*p == '\0')
            break;
        /* Check if this line starts with the key */
        if (strncmp(p, key, klen) == 0 &&
            (p[klen] == ' ' || p[klen] == '\t'))
        {
            const char *v = p + klen;
            while (*v == ' ' || *v == '\t')
                ++v;
            return v;
        }
        /* Skip to next line */
        while (*p && *p != '\n')
            ++p;
    }
    return NULL;
}

/* Parse an integer value from the config, or return the default. */
static int cfg_int(const char *buf, const char *key, int def)
{
    const char *v = cfg_find(buf, key);
    return v ? atoi(v) : def;
}

/* Read the entire file into a NUL-terminated buffer. Caller must free(). */
static char *read_file(const char *path)
{
    FILE *fp = fopen(path, "r");
    if (!fp)
        return NULL;
    if (fseek(fp, 0, SEEK_END) != 0)
    {
        fclose(fp);
        return NULL;
    }
    long sz = ftell(fp);
    if (sz < 0)
    {
        fclose(fp);
        return NULL;
    }
    rewind(fp);
    char *buf = (char *)malloc((size_t)sz + 1);
    if (!buf)
    {
        fclose(fp);
        return NULL;
    }
    size_t rd = fread(buf, 1, (size_t)sz, fp);
    buf[rd] = '\0';
    fclose(fp);
    return buf;
}

/*
 * 2. Interactive input utilities
 */

/*
 * Read one line from STDIN_FILENO byte-by-byte. Works correctly with
 * both terminals and pipes. Truncates if the buffer is too small.
 */
static void read_line(char *buf, size_t sz)
{
    size_t i = 0;
    for (;;)
    {
        char c;
        ssize_t n = read(STDIN_FILENO, &c, 1);
        if (n <= 0)
            break;
        if (c == '\n')
            break;
        if (i + 1 < sz)
            buf[i++] = c;
        /* If buffer is full, just consume the rest of the line */
    }
    buf[i] = '\0';
}

/*
 * Prompt for an integer in [lo..hi]. Empty input returns the default.
 * Repeats on invalid input with an error message.
 */
static int ask_int(const char *prompt, int def, int lo, int hi)
{
    if (def < lo)
        def = lo;
    if (def > hi)
        def = hi;

    for (;;)
    {
        io_printf("%s [%d]: ", prompt, def);
        char line[64];
        read_line(line, sizeof(line));

        const char *p = line;
        while (*p == ' ' || *p == '\t')
            ++p;
        if (*p == '\0')
            return def;

        char *end = NULL;
        long v = strtol(p, &end, 10);
        if (end == p)
        {
            io_printf("  ! ожидалось целое число\n");
            continue;
        }
        while (*end == ' ' || *end == '\t')
            ++end;
        if (*end != '\0')
        {
            io_printf("  ! лишние символы после числа\n");
            continue;
        }
        if (v < lo || v > hi)
        {
            io_printf("  ! допустимый диапазон: [%d..%d]\n", lo, hi);
            continue;
        }
        return (int)v;
    }
}

/* Prompt for a pair of coordinates "x y" with independent ranges. */
static void ask_pair(const char *prompt,
                     int defx, int defy,
                     int lox, int hix, int loy, int hiy,
                     int *outx, int *outy)
{
    if (defx < lox)
        defx = lox;
    if (defx > hix)
        defx = hix;
    if (defy < loy)
        defy = loy;
    if (defy > hiy)
        defy = hiy;

    for (;;)
    {
        io_printf("%s [%d %d]: ", prompt, defx, defy);
        char line[64];
        read_line(line, sizeof(line));

        const char *p = line;
        while (*p == ' ' || *p == '\t')
            ++p;
        if (*p == '\0')
        {
            *outx = defx;
            *outy = defy;
            return;
        }

        char *end = NULL;
        long x = strtol(p, &end, 10);
        if (end == p)
        {
            io_printf("  ! ожидалось \"x y\"\n");
            continue;
        }

        while (*end == ' ' || *end == '\t')
            ++end;
        const char *sy = end;
        long y = strtol(sy, &end, 10);
        if (end == sy)
        {
            io_printf("  ! не найдена координата y\n");
            continue;
        }
        while (*end == ' ' || *end == '\t')
            ++end;
        if (*end != '\0')
        {
            io_printf("  ! лишние символы после y\n");
            continue;
        }

        if (x < lox || x > hix || y < loy || y > hiy)
        {
            io_printf("  ! x должен быть в [%d..%d], y — в [%d..%d]\n",
                      lox, hix, loy, hiy);
            continue;
        }
        *outx = (int)x;
        *outy = (int)y;
        return;
    }
}

/* Prompt for a yes/no answer. Returns 1 for yes, 0 for no. */
static int ask_yes_no(const char *prompt, int def)
{
    for (;;)
    {
        io_printf("%s [%c]: ", prompt, def ? 'y' : 'n');
        char line[16];
        read_line(line, sizeof(line));
        const char *p = line;
        while (*p == ' ' || *p == '\t')
            ++p;
        if (*p == '\0')
            return def;
        if (*p == 'y' || *p == 'Y' || *p == '1')
            return 1;
        if (*p == 'n' || *p == 'N' || *p == '0')
            return 0;
        io_printf("  ! ответьте y или n\n");
    }
}

/* Prompt for a choice from a list of options. Returns the selected index. */
static int ask_choice(const char *prompt, const char **names, int count, int def)
{
    io_printf("%s\n", prompt);
    for (int i = 0; i < count; ++i)
        io_printf("    %d) %s\n", i, names[i]);
    return ask_int("  Выбор", def, 0, count - 1);
}

/*
 * 3. Cell occupancy checks
 */

/* Check if a cell can be used for an obstacle (empty and not occupied). */
static int cell_can_be_obstacle(const SimContext *ctx, int x, int y)
{
    if (!field_in_bounds(&ctx->field, x, y))
        return 0;
    if (ctx->field.cells[y * ctx->field.w + x] != CELL_EMPTY)
        return 0;
    for (int i = 0; i < ctx->tank_count; ++i)
        if (ctx->tanks[i].x == x && ctx->tanks[i].y == y)
            return 0;
    for (int i = 0; i < ctx->gun_count; ++i)
        if (ctx->guns[i].x == x && ctx->guns[i].y == y)
            return 0;
    return 1;
}

/* Check if a cell is free for a tank (passable and not occupied). */
static int cell_free_for_tank(const SimContext *ctx, int x, int y)
{
    if (!field_is_passable(&ctx->field, x, y))
        return 0;
    for (int i = 0; i < ctx->tank_count; ++i)
        if (ctx->tanks[i].alive &&
            ctx->tanks[i].x == x && ctx->tanks[i].y == y)
            return 0;
    return 1;
}

/* Check if a cell is free for a gun (passable and not occupied). */
static int cell_free_for_gun(const SimContext *ctx, int x, int y)
{
    if (!field_is_passable(&ctx->field, x, y))
        return 0;
    for (int i = 0; i < ctx->tank_count; ++i)
        if (ctx->tanks[i].alive &&
            ctx->tanks[i].x == x && ctx->tanks[i].y == y)
            return 0;
    for (int i = 0; i < ctx->gun_count; ++i)
        if (ctx->guns[i].x == x && ctx->guns[i].y == y)
            return 0;
    return 1;
}

/*
 * 4. Interactive input sections
 */

/* Prompt for obstacles: count and placement (random or manual). */
static void ask_obstacles(SimContext *ctx)
{
    io_printf("\n[Препятствия]\n");
    int maxn = ctx->field.w * ctx->field.h - 1;
    if (maxn < 0)
        maxn = 0;

    int n = ask_int("  Сколько препятствий?", 3, 0, maxn);
    if (n == 0)
        return;

    int randomize = ask_yes_no("  Разместить случайно?", 1);

    if (randomize)
    {
        int placed = 0, attempts = 0;
        int max_attempts = n * 1000 + 100;
        while (placed < n && attempts < max_attempts)
        {
            int x = rng_int(&ctx->rng, 0, ctx->field.w - 1);
            int y = rng_int(&ctx->rng, 0, ctx->field.h - 1);
            if (cell_can_be_obstacle(ctx, x, y))
            {
                field_set_obstacle(&ctx->field, x, y);
                placed++;
            }
            attempts++;
        }
        io_printf("  размещено %d из %d препятствий\n", placed, n);
        return;
    }

    /* Manual placement */
    for (int i = 0; i < n; ++i)
    {
        char prompt[64];
        snprintf(prompt, sizeof(prompt), "  Препятствие #%d: x y", i + 1);
        for (;;)
        {
            int x, y;
            int dx = rng_int(&ctx->rng, 0, ctx->field.w - 1);
            int dy = rng_int(&ctx->rng, 0, ctx->field.h - 1);
            ask_pair(prompt, dx, dy,
                     0, ctx->field.w - 1, 0, ctx->field.h - 1, &x, &y);
            if (!cell_can_be_obstacle(ctx, x, y))
            {
                io_printf("  ! клетка (%d,%d) уже занята\n", x, y);
                continue;
            }
            field_set_obstacle(&ctx->field, x, y);
            break;
        }
    }
}

/* Prompt for tanks: position, HP, strategy, and strategy-specific parameters. */
static void ask_tanks(SimContext *ctx)
{
    io_printf("\n[Танки]\n");
    static const char *TANK_NAMES[] = {
        "stay", "random_walk", "straight", "patrol"};

    int n = ask_int("  Сколько танков?", 2, 0, MAX_TANKS);

    for (int i = 0; i < n; ++i)
    {
        Tank *t = &ctx->tanks[ctx->tank_count];
        memset(t, 0, sizeof(*t));
        t->id = i + 1;
        io_printf("  Танк #%d:\n", t->id);

        /* Position with collision check */
        for (;;)
        {
            int dx = rng_int(&ctx->rng, 0, ctx->field.w - 1);
            int dy = rng_int(&ctx->rng, 0, ctx->field.h - 1);
            ask_pair("    Позиция x y", dx, dy,
                     0, ctx->field.w - 1, 0, ctx->field.h - 1,
                     &t->x, &t->y);
            if (cell_free_for_tank(ctx, t->x, t->y))
                break;
            io_printf("  ! клетка (%d,%d) занята препятствием или танком\n",
                      t->x, t->y);
        }

        t->hp = t->max_hp = ask_int("    Прочность (hp)", 10, 1, 100000);

        t->strategy = (tank_strategy_t)ask_choice(
            "    Стратегия движения:",
            TANK_NAMES, 4, TANK_RANDOM_WALK);

        t->alive = 1;
        t->born_tick = 0;

        /* Strategy-specific parameters */
        if (t->strategy == TANK_STRAIGHT)
        {
            static const char *DIRS[] = {
                "вверх (0,-1)", "вправо (+1,0)", "вниз (0,+1)", "влево (-1,0)"};
            static const int DX[4] = {0, 1, 0, -1};
            static const int DY[4] = {-1, 0, 1, 0};
            int d = ask_choice("    Начальное направление:", DIRS, 4, 0);
            t->dx = DX[d];
            t->dy = DY[d];
        }
        else if (t->strategy == TANK_PATROL)
        {
            io_printf("    Две точки маршрута:\n");
            for (;;)
            {
                int dx = rng_int(&ctx->rng, 0, ctx->field.w - 1);
                int dy = rng_int(&ctx->rng, 0, ctx->field.h - 1);
                ask_pair("      Точка A (x y)", dx, dy,
                         0, ctx->field.w - 1, 0, ctx->field.h - 1,
                         &t->patrol_x0, &t->patrol_y0);
                if (field_is_passable(&ctx->field,
                                      t->patrol_x0, t->patrol_y0))
                    break;
                io_printf("  ! точка A попала в препятствие\n");
            }
            for (;;)
            {
                int dx = rng_int(&ctx->rng, 0, ctx->field.w - 1);
                int dy = rng_int(&ctx->rng, 0, ctx->field.h - 1);
                ask_pair("      Точка B (x y)", dx, dy,
                         0, ctx->field.w - 1, 0, ctx->field.h - 1,
                         &t->patrol_x1, &t->patrol_y1);
                if (field_is_passable(&ctx->field,
                                      t->patrol_x1, t->patrol_y1))
                    break;
                io_printf("  ! точка B попала в препятствие\n");
            }
            t->patrol_leg = 0;
        }

        ctx->tank_count++;
    }
}

/* Prompt for guns: position, ammo, range, reload, accuracy, etc. */
static void ask_guns(SimContext *ctx)
{
    io_printf("\n[Орудия]\n");
    static const char *GUN_NAMES[] = {"random", "last", "predict"};

    int n = ask_int("  Сколько орудий?", 1, 0, MAX_GUNS);

    for (int i = 0; i < n; ++i)
    {
        Oreshnik *g = &ctx->guns[ctx->gun_count];
        memset(g, 0, sizeof(*g));
        g->id = i + 1;
        io_printf("  Орудие #%d:\n", g->id);

        for (;;)
        {
            int dx = rng_int(&ctx->rng, 0, ctx->field.w - 1);
            int dy = rng_int(&ctx->rng, 0, ctx->field.h - 1);
            ask_pair("    Позиция x y", dx, dy,
                     0, ctx->field.w - 1, 0, ctx->field.h - 1,
                     &g->x, &g->y);
            if (cell_free_for_gun(ctx, g->x, g->y))
                break;
            io_printf("  ! клетка (%d,%d) занята\n", g->x, g->y);
        }

        g->ammo = ask_int("    Боезапас", 8, 0, 100000);
        g->range = ask_int("    Дальность наблюдения (клеток, Манхэттен)",
                           10, 0, 1000);
        g->reload = ask_int("    Перезарядка (тактов)", 2, 1, 1000);
        g->shot_delay = ask_int("    Задержка полёта снаряда (тактов)",
                                2, 0, 1000);
        g->accuracy = ask_int("    Точность прицела (0..100)", 70, 0, 100);
        g->blast_radius = ask_int("    Радиус поражения (0 = только клетка)",
                                  1, 0, 50);
        g->damage_direct = ask_int("    Урон в клетке попадания", 10, 1, 100000);
        g->damage_splash = ask_int("    Урон в радиусе", 3, 0, 100000);
        g->strategy = (gun_strategy_t)ask_choice(
            "    Стратегия прицеливания:", GUN_NAMES, 3, GUN_PREDICT);

        g->prev_x = g->prev_y = -1;
        ctx->gun_count++;
    }
}

/*
 * Full interactive scenario. Order: seed → field → obstacles → tanks →
 * guns → simulation parameters.
 */
static int config_interactive(SimContext *ctx, unsigned seed,
                              int delay_ms, long long max_ticks)
{
    io_printf("\n=== Интерактивная настройка симуляции ===\n"
              "Пустой ввод — принять значение по умолчанию в [].\n");

    /* Seed: used for random hints during placement */
    seed = (unsigned)ask_int("\n[ГСЧ]\n  Seed (0 = случайный)",
                             (int)seed, 0, 0x7fffffff);
    if (seed == 0)
        seed = (unsigned)time(NULL);
    rng_seed(&ctx->rng, seed);

    /* Field dimensions */
    io_printf("\n[Поле]\n");
    int w = ask_int("  Ширина", 12, 2, 200);
    int h = ask_int("  Высота", 8, 2, 200);
    if (field_init(&ctx->field, w, h) != 0)
        return -1;

    ask_obstacles(ctx);
    ask_tanks(ctx);
    ask_guns(ctx);

    /* Simulation parameters */
    io_printf("\n[Параметры симуляции]\n");
    ctx->delay_ms = ask_int("  Задержка между тактами, мс",
                            delay_ms, 0, 5000);
    ctx->max_ticks = ask_int("  Предел тактов (0 = без предела)",
                             (int)max_ticks, 0, 10000000);
    ctx->verbose_field = ask_yes_no(
        "  Печатать состояние поля каждый такт?", 0);

    /* Summary for user confirmation */
    io_printf("\n--- Конфигурация готова ---\n");
    io_printf("  Поле %dx%d, seed=%u\n", ctx->field.w, ctx->field.h, seed);
    io_printf("  Танков: %d, орудий: %d\n",
              ctx->tank_count, ctx->gun_count);
    io_printf("  delay=%d мс, max_ticks=%lld\n",
              ctx->delay_ms, (long long)ctx->max_ticks);

    return 0;
}

/*
 * 5. Default scenario (non-interactive launch)
 */

/* Load a demo scenario: 12x8 field, 2 tanks, 1 gun, 3 obstacles. */
static void load_defaults(SimContext *ctx, unsigned seed,
                          int delay_ms, long long max_ticks)
{
    rng_seed(&ctx->rng, seed);
    ctx->delay_ms = delay_ms;
    ctx->max_ticks = max_ticks;
    ctx->verbose_field = 1;

    field_init(&ctx->field, 12, 8);
    field_set_obstacle(&ctx->field, 3, 2);
    field_set_obstacle(&ctx->field, 7, 5);
    field_set_obstacle(&ctx->field, 5, 1);

    Tank *t = &ctx->tanks[ctx->tank_count++];
    memset(t, 0, sizeof(*t));
    t->id = 1;
    t->x = 2;
    t->y = 3;
    t->hp = t->max_hp = 10;
    t->alive = 1;
    t->strategy = TANK_RANDOM_WALK;

    t = &ctx->tanks[ctx->tank_count++];
    memset(t, 0, sizeof(*t));
    t->id = 2;
    t->x = 9;
    t->y = 6;
    t->hp = t->max_hp = 10;
    t->alive = 1;
    t->strategy = TANK_STRAIGHT;
    t->dx = -1;
    t->dy = 0;

    Oreshnik *g = &ctx->guns[ctx->gun_count++];
    memset(g, 0, sizeof(*g));
    g->id = 1;
    g->x = 0;
    g->y = 0;
    g->ammo = 8;
    g->range = 10;
    g->reload = 2;
    g->shot_delay = 2;
    g->accuracy = 70;
    g->blast_radius = 1;
    g->damage_direct = 10;
    g->damage_splash = 3;
    g->strategy = GUN_PREDICT;
    g->prev_x = g->prev_y = -1;
}

/*
 * 6. Public API
 */

void config_usage(const char *progname)
{
    io_printf(
        "Usage: %s [options]\n"
        "  --seed N         начальное значение ГСЧ (по умолчанию 42)\n"
        "  --delay MS       задержка между тактами, мс (по умолчанию 100)\n"
        "  --max-ticks N    предел тактов, 0 = без предела (по умолчанию 200)\n"
        "  --config FILE    прочитать параметры из файла\n"
        "  --log FILE       писать зеркало журнала в файл\n"
        "  --no-field       не печатать поле каждый такт\n"
        "  --interactive    спросить параметры в интерактиве\n"
        "  --help           эта справка\n",
        progname);
}

int config_load(SimContext *ctx, int argc, char **argv)
{
    unsigned int seed = 42;
    int delay_ms = 100;
    long long max_ticks = 200;
    int interactive = 0;
    int no_field = 0;
    const char *cfg_path = NULL;

    /* Parse CLI arguments */
    for (int i = 1; i < argc; ++i)
    {
        const char *a = argv[i];
        if (!strcmp(a, "--help"))
        {
            config_usage(argv[0]);
            return -1;
        }
        else if (!strcmp(a, "--seed") && i + 1 < argc)
            seed = (unsigned)atoi(argv[++i]);
        else if (!strcmp(a, "--delay") && i + 1 < argc)
            delay_ms = atoi(argv[++i]);
        else if (!strcmp(a, "--max-ticks") && i + 1 < argc)
            max_ticks = atoll(argv[++i]);
        else if (!strcmp(a, "--config") && i + 1 < argc)
            cfg_path = argv[++i];
        else if (!strcmp(a, "--interactive"))
            interactive = 1;
        else if (!strcmp(a, "--no-field"))
            no_field = 1;
    }

    /* Load config file if specified (CLI args override it) */
    char *cfg_buf = NULL;
    if (cfg_path)
    {
        cfg_buf = read_file(cfg_path);
        if (!cfg_buf)
        {
            io_printf("Не удалось открыть конфиг: %s\n", cfg_path);
            return -1;
        }
        seed = (unsigned)cfg_int(cfg_buf, "seed", (int)seed);
        delay_ms = cfg_int(cfg_buf, "delay_ms", delay_ms);
        max_ticks = cfg_int(cfg_buf, "max_ticks", (int)max_ticks);
    }

    int rc = 0;
    if (interactive)
    {
        rc = config_interactive(ctx, seed, delay_ms, max_ticks);
    }
    else
    {
        load_defaults(ctx, seed, delay_ms, max_ticks);
    }
    if (no_field)
        ctx->verbose_field = 0;

    if (cfg_buf)
        free(cfg_buf);
    return rc;
}