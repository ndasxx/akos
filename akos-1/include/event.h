#ifndef EVENT_H
#define EVENT_H

#include <stddef.h>
#include "common.h"

/**
 * \file event.h
 * \brief Event types and priority queue for discrete event simulation.
 */

/**
 * \brief Event types with associated priorities
 *
 * The numeric value determines the processing order within a single tick:
 * events with lower priority values are processed first when times are equal.
 * The order ensures that tank movements happen before gun observations and
 * firing, and projectile impacts are resolved last.
 */
typedef enum
{
    EV_TANK_MOVE = 10,         /**< Tank movement */
    EV_GUN_OBSERVE = 20,       /**< Gun observation */
    EV_GUN_FIRE = 30,          /**< Gun firing */
    EV_PROJECTILE_IMPACT = 40, /**< Projectile impact */
    EV_END_CHECK = 90          /**< End condition check */
} event_type_t;

/**
 * \brief Single event in the calendar
 */
typedef struct
{
    long long time;    /**< Model time (tick number) */
    int priority;      /**< Secondary key (from event_type_t) */
    unsigned long seq; /**< Tertiary key */
    event_type_t type; /**< Event type */
    int entity_id;     /**< ID of the entity (tank/gun/projectile) */
} Event;

/**
 * \brief Event queue ordered by (time, priority, seq)
 *
 */
typedef struct
{
    Event *data;            /**< Heap array */
    size_t size;            /**< Number of elements */
    size_t cap;             /**< Allocated capacity */
    unsigned long next_seq; /**< Next sequence number for tie-breaking */
} EventQueue;

/**
 * \brief Initializes the event queue
 * \param q           Pointer to the queue
 * \param initial_cap Initial capacity (minimum 8)
 * \return STATUS_OK on success, STATUS_ERR on allocation failure
 */
int eq_init(EventQueue *q, size_t initial_cap);

/**
 * \brief Frees the event queue
 * \param q Pointer to the queue
 */
void eq_free(EventQueue *q);

/**
 * \brief Pushes a new event into the queue
 * \param q         Pointer to the queue
 * \param type      Event type
 * \param entity_id ID of the entity
 * \param time      Model time when the event occurs
 * \return STATUS_OK on success, STATUS_ERR on allocation failure
 *
 */
int eq_push(EventQueue *q, event_type_t type, int entity_id, long long time);

/**
 * \brief Pops the next event from the queue
 * \param q   Pointer to the queue
 * \param out Pointer to store the event
 * \return STATUS_OK on success, STATUS_ERR if the queue is empty
 */
int eq_pop(EventQueue *q, Event *out);

/**
 * \brief Checks if the queue is empty
 * \param q Pointer to the queue
 * \return Non-zero if empty, 0 otherwise
 */
int eq_empty(const EventQueue *q);

/**
 * \brief Returns the number of events in the queue
 * \param q Pointer to the queue
 * \return Number of events
 */
size_t eq_size(const EventQueue *q);

#endif /* EVENT_H */