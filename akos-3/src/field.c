/**
 * \file field.c
 * \brief Implementation of the shared-memory field operations.
 */

#include "field.h"
#include <string.h>
#include "common.h"

int field_init(ShmField *f, int w, int h)
{
    if (w <= 0 || h <= 0 || w > MAX_FIELD_W || h > MAX_FIELD_H)
        return STATUS_ERR;
    f->w = w;
    f->h = h;
    memset(f->cells, 0, sizeof(f->cells));
    return STATUS_OK;
}

int field_in_bounds(const ShmField *f, int x, int y)
{
    return x >= 0 && y >= 0 && x < f->w && y < f->h;
}

int field_is_passable(const ShmField *f, int x, int y)
{
    if (!field_in_bounds(f, x, y))
        return 0;
    return f->cells[y * f->w + x] == CELL_EMPTY;
}

void field_set_obstacle(ShmField *f, int x, int y)
{
    if (field_in_bounds(f, x, y))
        f->cells[y * f->w + x] = CELL_OBSTACLE;
}

void field_set_wreck(ShmField *f, int x, int y)
{
    if (field_in_bounds(f, x, y))
        f->cells[y * f->w + x] = CELL_WRECK;
}

void field_print(const ShmField *f, char *buf, int bufsz)
{
    int pos = 0;
    for (int y = 0; y < f->h; ++y)
    {
        for (int x = 0; x < f->w; ++x)
        {
            char c;
            switch (f->cells[y * f->w + x])
            {
            case CELL_OBSTACLE:
                c = '#';
                break;
            case CELL_WRECK:
                c = 'X';
                break;
            default:
                c = '.';
                break;
            }
            if (pos + 2 < bufsz)
                buf[pos++] = c;
        }
        if (pos + 1 < bufsz)
            buf[pos++] = '\n';
    }
    buf[pos < bufsz ? pos : bufsz - 1] = '\0';
}
