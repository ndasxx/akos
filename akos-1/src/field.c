/**
 * \file field.c
 * \brief Implementation of the rectangular field.
 */

#include "field.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "common.h"

int field_init(Field *f, int w, int h)
{
    if (w <= 0 || h <= 0)
        return -1;

    f->w = w;
    f->h = h;
    /* calloc initializes all cells to CELL_EMPTY (0) */
    f->cells = (cell_t *)calloc((size_t)w * (size_t)h, sizeof(cell_t));
    return f->cells ? STATUS_OK : STATUS_ERR;
}

void field_free(Field *f)
{
    free(f->cells);
    f->cells = NULL;
    f->w = f->h = 0;
}

int field_in_bounds(const Field *f, int x, int y)
{
    return x >= 0 && y >= 0 && x < f->w && y < f->h;
}

/* A cell is passable only if it is within bounds and is CELL_EMPTY. */
int field_is_passable(const Field *f, int x, int y)
{
    if (!field_in_bounds(f, x, y))
        return 0;
    return f->cells[y * f->w + x] == CELL_EMPTY;
}

void field_set_obstacle(Field *f, int x, int y)
{
    if (field_in_bounds(f, x, y))
        f->cells[y * f->w + x] = CELL_OBSTACLE;
}

void field_set_wreck(Field *f, int x, int y)
{
    if (field_in_bounds(f, x, y))
        f->cells[y * f->w + x] = CELL_WRECK;
}

void field_print(const Field *f, char *buf, int bufsz)
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
            /* Leave room for the character and a potential newline */
            if (pos + 2 < bufsz)
                buf[pos++] = c;
        }
        /* Add newline after each row */
        if (pos + 1 < bufsz)
            buf[pos++] = '\n';
    }

    /* Ensure NUL-termination, even if the buffer was truncated */
    buf[pos < bufsz ? pos : bufsz - 1] = '\0';
}