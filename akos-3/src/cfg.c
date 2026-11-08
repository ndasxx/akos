/**
 * \file cfg.c
 * \brief Implementation of configuration loading and state initialization.
 */

#include "cfg.h"
#include "rng.h"
#include "field.h"
#include "logic.h"
#include "io.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>

void cfg_defaults(SimConfig *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->field_w = 12;
    cfg->field_h = 12;
    cfg->obstacles = 8;

    cfg->tanks = 3;
    cfg->tank_hp = 3;
    cfg->tank_strategy = TANK_RANDOM_WALK;

    cfg->guns = 2;
    cfg->ammo = 25;
    cfg->range = 7;
    cfg->reload = 2;
    cfg->shot_delay = 2;
    cfg->accuracy = 70;
    cfg->blast_radius = 1;
    cfg->damage_direct = 3;
    cfg->damage_splash = 1;
    cfg->gun_strategy = GUN_PREDICT;

    cfg->seed = 0;
    cfg->max_ticks = 100;
    cfg->delay_ms = 50;
}

/* ------------------------------------------------------------------ */
/* Config file parsing ("key value" lines, '#' starts a comment)       */
/* ------------------------------------------------------------------ */

static const char *cfg_find(const char *buf, const char *key)
{
    size_t klen = strlen(key);
    const char *p = buf;
    while (p && *p)
    {
        while (*p == ' ' || *p == '\t')
            ++p;
        if (*p == '#' || *p == '\n')
        {
            while (*p && *p != '\n')
                ++p;
            continue;
        }
        if (*p == '\0')
            break;
        if (strncmp(p, key, klen) == 0 && (p[klen] == ' ' || p[klen] == '\t'))
        {
            const char *v = p + klen;
            while (*v == ' ' || *v == '\t')
                ++v;
            return v;
        }
        while (*p && *p != '\n')
            ++p;
    }
    return NULL;
}

static int cfg_int(const char *buf, const char *key, int def)
{
    const char *v = cfg_find(buf, key);
    return v ? atoi(v) : def;
}

static unsigned int cfg_uint(const char *buf, const char *key, unsigned int def)
{
    const char *v = cfg_find(buf, key);
    return v ? (unsigned int)strtoul(v, NULL, 10) : def;
}

static void cfg_str(const char *buf, const char *key, const char *def,
                    char *out, size_t outsz)
{
    const char *v = cfg_find(buf, key);
    snprintf(out, outsz, "%s", v ? v : def);
}

static char *read_file(const char *path)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return NULL;
    char *buf = (char *)malloc(16384);
    if (!buf)
    {
        close(fd);
        return NULL;
    }
    ssize_t rd = read(fd, buf, 16383);
    close(fd);
    if (rd < 0)
    {
        free(buf);
        return NULL;
    }
    buf[rd] = '\0';
    return buf;
}

/* ------------------------------------------------------------------ */
/* CLI parsing                                                         */
/* ------------------------------------------------------------------ */

/* Find the value following a key in argv. Returns NULL if not found. */
static const char *find_arg(int argc, char **argv, const char *key)
{
    for (int i = 1; i + 1 < argc; ++i)
        if (!strcmp(argv[i], key))
            return argv[i + 1];
    return NULL;
}

static int has_flag(int argc, char **argv, const char *key)
{
    for (int i = 1; i < argc; ++i)
        if (!strcmp(argv[i], key))
            return 1;
    return 0;
}

static tank_strategy_t parse_tank_strategy(const char *s)
{
    if (!s)
        return TANK_RANDOM_WALK;
    if (!strcmp(s, "stay"))
        return TANK_STAY;
    if (!strcmp(s, "straight"))
        return TANK_STRAIGHT;
    if (!strcmp(s, "patrol"))
        return TANK_PATROL;
    return TANK_RANDOM_WALK;
}

static gun_strategy_t parse_gun_strategy(const char *s)
{
    if (!s)
        return GUN_PREDICT;
    if (!strcmp(s, "random"))
        return GUN_RANDOM;
    if (!strcmp(s, "last"))
        return GUN_AIM_LAST;
    return GUN_PREDICT;
}

/* ------------------------------------------------------------------ */
/* Interactive input                                                   */
/* ------------------------------------------------------------------ */

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
    }
    buf[i] = '\0';
}

static int ask_int(const char *prompt, int def, int lo, int hi)
{
    char line[64];
    for (;;)
    {
        io_printf("%s [%d]: ", prompt, def);
        read_line(line, sizeof(line));
        if (line[0] == '\0')
            return def;
        char *end = NULL;
        long v = strtol(line, &end, 10);
        if (end != line && v >= lo && v <= hi)
            return (int)v;
        io_printf("  Некорректное значение: введите целое от %d до %d "
                  "(или пустой ввод для значения по умолчанию)\n",
                  lo, hi);
    }
}

static int ask_yes_no(const char *prompt, int def)
{
    char line[16];
    for (;;)
    {
        io_printf("%s [%c]: ", prompt, def ? 'y' : 'n');
        read_line(line, sizeof(line));
        if (line[0] == '\0')
            return def;
        if (line[0] == 'y' || line[0] == 'Y')
            return 1;
        if (line[0] == 'n' || line[0] == 'N')
            return 0;
        io_printf("  Введите y или n (или пустой ввод)\n");
    }
}

static int ask_choice(const char *prompt, const char **names,
                      int count, int def)
{
    char line[64];
    for (;;)
    {
        io_printf("%s\n", prompt);
        for (int i = 0; i < count; ++i)
            io_printf("  %d) %s%s\n", i + 1, names[i],
                      i == def ? "  (по умолчанию)" : "");
        io_printf("  Выбор [%d]: ", def + 1);
        read_line(line, sizeof(line));
        if (line[0] == '\0')
            return def;
        int n = atoi(line);
        if (n >= 1 && n <= count)
            return n - 1;
        for (int i = 0; i < count; ++i)
            if (!strcmp(line, names[i]))
                return i;
        io_printf("  Некорректный выбор\n");
    }
}

static int cfg_interactive(SimConfig *cfg)
{
    static const char *tank_names[] = {"stay", "random_walk", "straight", "patrol"};
    static const char *gun_names[] = {"random", "last", "predict"};

    io_printf("\n=== Интерактивная настройка симуляции ===\n"
              "Пустой ввод — принять значение в [].\n");

    io_printf("\n[ГСЧ]\n");
    cfg->seed = (unsigned int)ask_int("  Seed (0 = случайный)",
                                      (int)cfg->seed, 0, 0x7fffffff);

    io_printf("\n[Поле]\n");
    cfg->field_w = ask_int("  Ширина", cfg->field_w, 1, MAX_FIELD_W);
    cfg->field_h = ask_int("  Высота", cfg->field_h, 1, MAX_FIELD_H);
    cfg->obstacles = ask_int("  Число препятствий", cfg->obstacles, 0, 64);

    io_printf("\n[Танки]\n");
    cfg->tanks = ask_int("  Число танков", cfg->tanks, 0, MAX_TANKS);
    cfg->tank_hp = ask_int("  Прочность танка", cfg->tank_hp, 1, 100);
    cfg->tank_strategy = (tank_strategy_t)ask_choice(
        "  Стратегия движения:", tank_names, 4, (int)cfg->tank_strategy);

    io_printf("\n[Орудия]\n");
    cfg->guns = ask_int("  Число орудий", cfg->guns, 0, MAX_GUNS);
    cfg->ammo = ask_int("  Боезапас орудия", cfg->ammo, 0, 100000);
    cfg->range = ask_int("  Дальность наблюдения", cfg->range, 1, 64);
    cfg->reload = ask_int("  Перезарядка, тактов", cfg->reload, 1, 1000);
    cfg->shot_delay = ask_int("  Задержка снаряда, тактов",
                              cfg->shot_delay, 0, 1000);
    cfg->accuracy = ask_int("  Точность, %", cfg->accuracy, 0, 100);
    cfg->blast_radius = ask_int("  Радиус поражения", cfg->blast_radius, 0, 16);
    cfg->damage_direct = ask_int("  Урон: прямое попадание",
                                 cfg->damage_direct, 1, 1000);
    cfg->damage_splash = ask_int("  Урон: близкий разрыв",
                                 cfg->damage_splash, 0, 1000);
    cfg->gun_strategy = (gun_strategy_t)ask_choice(
        "  Стратегия прицеливания:", gun_names, 3, (int)cfg->gun_strategy);

    io_printf("\n[Параметры симуляции]\n");
    cfg->max_ticks = ask_int("  Предел тактов (0 = без предела)",
                             (int)cfg->max_ticks, 0, 10000000);
    cfg->delay_ms = ask_int("  Задержка между тактами, мс",
                            cfg->delay_ms, 0, 5000);
    cfg->verbose_field = ask_yes_no("  Печатать поле каждый такт?",
                                    cfg->verbose_field);

    io_printf("\n--- Конфигурация готова ---\n");
    io_printf("  Поле %dx%d, препятствий: %d, seed=%u\n",
              cfg->field_w, cfg->field_h, cfg->obstacles, cfg->seed);
    io_printf("  Танков: %d (%s), орудий: %d (%s)\n",
              cfg->tanks, tank_strategy_name(cfg->tank_strategy),
              cfg->guns, gun_strategy_name(cfg->gun_strategy));
    io_printf("  max_ticks=%lld, delay=%d мс\n",
              (long long)cfg->max_ticks, cfg->delay_ms);
    io_printf("  Размещение танков и орудий — автоматическое (по seed).\n");
    return 0;
}

int cfg_load(SimConfig *cfg, int argc, char **argv)
{
    /* Config file requested? */
    const char *file = find_arg(argc, argv, "--config");
    if (file)
    {
        snprintf(cfg->cfg_file, sizeof(cfg->cfg_file), "%s", file);
        char *buf = read_file(file);
        if (!buf)
        {
            io_printf("Не удалось прочитать конфигурационный файл: %s\n", file);
            return -1;
        }
        cfg->field_w = cfg_int(buf, "w", cfg->field_w);
        cfg->field_h = cfg_int(buf, "h", cfg->field_h);
        cfg->obstacles = cfg_int(buf, "obstacles", cfg->obstacles);
        cfg->tanks = cfg_int(buf, "tanks", cfg->tanks);
        cfg->tank_hp = cfg_int(buf, "tank-hp", cfg->tank_hp);
        cfg->tank_strategy = parse_tank_strategy(cfg_find(buf, "tank-strategy"));
        cfg->guns = cfg_int(buf, "guns", cfg->guns);
        cfg->ammo = cfg_int(buf, "ammo", cfg->ammo);
        cfg->range = cfg_int(buf, "range", cfg->range);
        cfg->reload = cfg_int(buf, "reload", cfg->reload);
        cfg->shot_delay = cfg_int(buf, "shot-delay", cfg->shot_delay);
        cfg->accuracy = cfg_int(buf, "accuracy", cfg->accuracy);
        cfg->blast_radius = cfg_int(buf, "blast-radius", cfg->blast_radius);
        cfg->damage_direct = cfg_int(buf, "damage-direct", cfg->damage_direct);
        cfg->damage_splash = cfg_int(buf, "damage-splash", cfg->damage_splash);
        cfg->gun_strategy = parse_gun_strategy(cfg_find(buf, "gun-strategy"));
        cfg->seed = cfg_uint(buf, "seed", cfg->seed);
        cfg->max_ticks = cfg_int(buf, "max-ticks", (int)cfg->max_ticks);
        cfg->delay_ms = cfg_int(buf, "delay", cfg->delay_ms);
        cfg_str(buf, "log", "", cfg->log_path, sizeof(cfg->log_path));
        free(buf);
    }

    /* CLI overrides */
    const char *v;
    if ((v = find_arg(argc, argv, "--w")))
        cfg->field_w = atoi(v);
    if ((v = find_arg(argc, argv, "--h")))
        cfg->field_h = atoi(v);
    if ((v = find_arg(argc, argv, "--obstacles")))
        cfg->obstacles = atoi(v);
    if ((v = find_arg(argc, argv, "--tanks")))
        cfg->tanks = atoi(v);
    if ((v = find_arg(argc, argv, "--tank-hp")))
        cfg->tank_hp = atoi(v);
    if ((v = find_arg(argc, argv, "--tank-strategy")))
        cfg->tank_strategy = parse_tank_strategy(v);
    if ((v = find_arg(argc, argv, "--guns")))
        cfg->guns = atoi(v);
    if ((v = find_arg(argc, argv, "--ammo")))
        cfg->ammo = atoi(v);
    if ((v = find_arg(argc, argv, "--range")))
        cfg->range = atoi(v);
    if ((v = find_arg(argc, argv, "--reload")))
        cfg->reload = atoi(v);
    if ((v = find_arg(argc, argv, "--shot-delay")))
        cfg->shot_delay = atoi(v);
    if ((v = find_arg(argc, argv, "--accuracy")))
        cfg->accuracy = atoi(v);
    if ((v = find_arg(argc, argv, "--blast-radius")))
        cfg->blast_radius = atoi(v);
    if ((v = find_arg(argc, argv, "--damage-direct")))
        cfg->damage_direct = atoi(v);
    if ((v = find_arg(argc, argv, "--damage-splash")))
        cfg->damage_splash = atoi(v);
    if ((v = find_arg(argc, argv, "--gun-strategy")))
        cfg->gun_strategy = parse_gun_strategy(v);
    if ((v = find_arg(argc, argv, "--seed")))
        cfg->seed = (unsigned int)strtoul(v, NULL, 10);
    if ((v = find_arg(argc, argv, "--max-ticks")))
        cfg->max_ticks = atoll(v);
    if ((v = find_arg(argc, argv, "--delay")))
        cfg->delay_ms = atoi(v);
    if ((v = find_arg(argc, argv, "--log")))
        snprintf(cfg->log_path, sizeof(cfg->log_path), "%s", v);
    if (has_flag(argc, argv, "--verbose-field"))
        cfg->verbose_field = 1;
    if (has_flag(argc, argv, "--help"))
        return 1;

    /* Interactive scenario: values already loaded (CLI/config) are
     * offered as defaults */
    if (has_flag(argc, argv, "--interactive"))
    {
        if (cfg_interactive(cfg) != 0)
            return -1;
    }

    /* Range validation */
    if (cfg->field_w < 1 || cfg->field_h < 1 ||
        cfg->field_w > MAX_FIELD_W || cfg->field_h > MAX_FIELD_H ||
        cfg->tanks < 0 || cfg->tanks > MAX_TANKS ||
        cfg->guns < 0 || cfg->guns > MAX_GUNS ||
        cfg->tank_hp < 1 || cfg->ammo < 0)
    {
        io_printf("Некорректные параметры конфигурации\n");
        return -1;
    }
    return 0;
}

void cfg_usage(const char *progname)
{
    io_printf("Использование: %s [опции]\n", progname);
    io_printf("  --w N --h N              размеры поля (по умолчанию 12x12)\n");
    io_printf("  --obstacles N            число случайных препятствий (8)\n");
    io_printf("  --tanks N                число танков (3)\n");
    io_printf("  --tank-hp N              прочность танка (3)\n");
    io_printf("  --tank-strategy S        stay|random_walk|straight|patrol\n");
    io_printf("  --guns N                 число орудий (2)\n");
    io_printf("  --ammo N                 боезапас орудия (25)\n");
    io_printf("  --range N                дальность наблюдения (7)\n");
    io_printf("  --reload N               интервал выстрелов в тактах (2)\n");
    io_printf("  --shot-delay N           задержка снаряда в тактах (2)\n");
    io_printf("  --accuracy N             точность %% (70)\n");
    io_printf("  --blast-radius N         радиус поражения (1)\n");
    io_printf("  --damage-direct N        урон при прямом попадании (3)\n");
    io_printf("  --damage-splash N        урон при близком разрыве (1)\n");
    io_printf("  --gun-strategy S         random|last|predict\n");
    io_printf("  --seed N                 зерно ГПСЧ (0 = время)\n");
    io_printf("  --max-ticks N            предел тактов (100)\n");
    io_printf("  --delay MS               задержка между тактами, мс (50)\n");
    io_printf("  --log FILE               журнал результатов\n");
    io_printf("  --verbose-field          печать поля каждый такт\n");
    io_printf("  --config FILE            файл конфигурации (key value)\n");
    io_printf("  --interactive            спросить параметры в интерактиве\n");
    io_printf("  --help                   эта справка\n");
}

/* ------------------------------------------------------------------ */
/* Initial state construction                                          */
/* ------------------------------------------------------------------ */

/* Picks a random free cell (no obstacle, no entity yet). */
static void pick_free_cell(const ShmField *f, Rng *rng, const Tank *tanks, int nt,
                           const Oreshnik *guns, int ng, int *out_x, int *out_y)
{
    for (int tries = 0; tries < 10000; ++tries)
    {
        int x = rng_int(rng, 0, f->w - 1);
        int y = rng_int(rng, 0, f->h - 1);
        if (!field_is_passable(f, x, y))
            continue;
        int busy = 0;
        for (int i = 0; i < nt; ++i)
            if (tanks[i].x == x && tanks[i].y == y)
                busy = 1;
        for (int i = 0; i < ng; ++i)
            if (guns[i].x == x && guns[i].y == y)
                busy = 1;
        if (!busy)
        {
            *out_x = x;
            *out_y = y;
            return;
        }
    }
    *out_x = 0;
    *out_y = 0;
}

int state_build(SharedState *st, const SimConfig *cfg)
{
    Rng rng;
    unsigned int seed = cfg->seed ? cfg->seed : (unsigned int)time(NULL);
    rng_seed(&rng, seed);

    if (field_init(&st->field, cfg->field_w, cfg->field_h) != STATUS_OK)
        return STATUS_ERR;

    st->seed = seed;
    st->max_ticks = cfg->max_ticks;
    st->delay_ms = cfg->delay_ms;
    st->verbose_field = cfg->verbose_field;

    /* Obstacles */
    int placed = 0;
    int wanted = cfg->obstacles;
    if (wanted > st->field.w * st->field.h / 4)
        wanted = st->field.w * st->field.h / 4;
    while (placed < wanted)
    {
        int x = rng_int(&rng, 0, st->field.w - 1);
        int y = rng_int(&rng, 0, st->field.h - 1);
        if (field_is_passable(&st->field, x, y))
        {
            field_set_obstacle(&st->field, x, y);
            placed++;
        }
    }

    /* Tanks */
    st->tank_count = cfg->tanks;
    for (int i = 0; i < cfg->tanks; ++i)
    {
        Tank *t = &st->tanks[i];
        t->id = i + 1;
        t->hp = t->max_hp = cfg->tank_hp;
        t->alive = 1;
        t->strategy = (tank_strategy_t)cfg->tank_strategy;
        t->dx = 1;
        t->dy = 0;
        pick_free_cell(&st->field, &rng, st->tanks, i, st->guns, 0,
                       &t->x, &t->y);
        t->patrol_x0 = t->x;
        t->patrol_y0 = t->y;
        pick_free_cell(&st->field, &rng, st->tanks, i, st->guns, 0,
                       &t->patrol_x1, &t->patrol_y1);
    }

    /* Guns */
    st->gun_count = cfg->guns;
    for (int i = 0; i < cfg->guns; ++i)
    {
        Oreshnik *g = &st->guns[i];
        g->id = i + 1;
        g->ammo = cfg->ammo;
        g->range = cfg->range;
        g->reload = cfg->reload;
        g->shot_delay = cfg->shot_delay;
        g->accuracy = cfg->accuracy;
        g->blast_radius = cfg->blast_radius;
        g->damage_direct = cfg->damage_direct;
        g->damage_splash = cfg->damage_splash;
        g->strategy = (gun_strategy_t)cfg->gun_strategy;
        g->next_fire = 0;
        pick_free_cell(&st->field, &rng, st->tanks, st->tank_count,
                       st->guns, i, &g->x, &g->y);
    }
    return STATUS_OK;
}
