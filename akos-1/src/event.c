/**
 * \file event.c
 * \brief Implementation of the event priority queue (binary min-heap).
 */

#include "event.h"
#include <stdlib.h>

/*
 * Compare two events: returns 1 if 'a' should be processed before 'b'.
 * Ordering: first by time, then by priority, then by sequence number.
 */
static int event_less(const Event *a, const Event *b)
{
    if (a->time != b->time)
        return a->time < b->time;
    if (a->priority != b->priority)
        return a->priority < b->priority;
    return a->seq < b->seq;
}

int eq_init(EventQueue *q, size_t initial_cap)
{
    /* Enforce a minimum capacity to avoid frequent reallocations */
    if (initial_cap < 8)
        initial_cap = 8;

    q->data = (Event *)malloc(initial_cap * sizeof(Event));
    if (!q->data)
        return STATUS_ERR;

    q->size = 0;
    q->cap = initial_cap;
    q->next_seq = 0;
    return STATUS_OK;
}

void eq_free(EventQueue *q)
{
    if (!q)
        return;
    free(q->data);
    q->data = NULL;
    q->size = 0;
    q->cap = 0;
    q->next_seq = 0;
}

/*
 * Double the capacity of the heap. Returns STATUS_ERR if realloc fails.
 * The queue state is not modified on failure (size remains unchanged).
 */
static int eq_grow(EventQueue *q)
{
    size_t new_cap = q->cap * 2;
    Event *nd = (Event *)realloc(q->data, new_cap * sizeof(Event));
    if (!nd)
        return STATUS_ERR;

    q->data = nd;
    q->cap = new_cap;
    return STATUS_OK;
}

/* Map event type to its priority (the numeric value of the enum). */
static int priority_of(event_type_t t)
{
    return (int)t;
}

int eq_push(EventQueue *q, event_type_t type, int entity_id, long long time)
{
    /* Grow the heap if necessary */
    if (q->size == q->cap)
    {
        if (eq_grow(q) != STATUS_OK)
            return STATUS_ERR;
    }

    Event e;
    e.time = time;
    e.priority = priority_of(type);
    e.seq = q->next_seq++;
    e.type = type;
    e.entity_id = entity_id;

    /* Place the new event at the end and sift it up */
    size_t i = q->size++;
    q->data[i] = e;

    while (i > 0)
    {
        size_t parent = (i - 1) / 2;
        if (event_less(&q->data[i], &q->data[parent]))
        {
            /* Swap with parent */
            Event tmp = q->data[i];
            q->data[i] = q->data[parent];
            q->data[parent] = tmp;
            i = parent;
        }
        else
        {
            break;
        }
    }
    return STATUS_OK;
}

int eq_pop(EventQueue *q, Event *out)
{
    if (q->size == 0)
        return STATUS_ERR;

    /* Extract the root (minimum element) */
    *out = q->data[0];
    q->size--;

    if (q->size > 0)
    {
        /* Move the last element to the root and sift it down */
        q->data[0] = q->data[q->size];

        size_t i = 0;
        for (;;)
        {
            size_t l = 2 * i + 1;
            size_t r = 2 * i + 2;
            size_t m = i;

            /* Find the smallest among i, left child, right child */
            if (l < q->size && event_less(&q->data[l], &q->data[m]))
                m = l;
            if (r < q->size && event_less(&q->data[r], &q->data[m]))
                m = r;

            if (m == i)
                break; /* Heap property restored */

            /* Swap with the smallest child */
            Event tmp = q->data[i];
            q->data[i] = q->data[m];
            q->data[m] = tmp;
            i = m;
        }
    }
    return STATUS_OK;
}

int eq_empty(const EventQueue *q)
{
    return q->size == 0;
}

size_t eq_size(const EventQueue *q)
{
    return q->size;
}