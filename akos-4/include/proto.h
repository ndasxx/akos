#ifndef PROTO_H
#define PROTO_H

/**
 * \file proto.h
 * \brief TCP message protocol of task #4 (client-server interaction).
 *
 * Every program connects to the server over TCP. The very first byte of
 * a connection selects the role of the client:
 *   - 'T' - tank client   (binary message protocol);
 *   - 'G' - gun client    (binary message protocol);
 *   - 'M' - monitor       (reads a full snapshot every tick, sends nothing);
 *   - 'C' - control       (text lines in both directions).
 *
 * Entity protocol (tanks and guns): the client sends PH_ATTACH and
 * receives PH_WELCOME (base seed and current tick), then every tick
 * answers the server's phase messages (PH_MOVE / PH_OBSERVE / PH_FIRE).
 * The server is the sole owner of the authoritative state: a client only
 * *proposes* a move or an aim point, the server validates and applies it.
 *
 * The message is a fixed-size structure — the same approach that the
 * FIFO implementation of task #3 used. TCP is a stream, so the helpers
 * net_write_full()/net_read_full() loop until the whole message is
 * transferred.
 */

#include <sys/types.h>
#include "state.h"

/** \brief Server default TCP port (overridden by --port) */
#define DEFAULT_PORT 7777

/**
 * \brief Message phases
 */
enum
{
    PH_ATTACH = 0, /**< Entity -> server: "I am here" (role, id, pid) */
    PH_WELCOME,    /**< Server -> entity: connection accepted (seed, tick) */
    PH_MOVE,       /**< Server -> tank: move context; tank -> server: chosen step */
    PH_OBSERVE,    /**< Server -> gun: observation context; gun -> server: new observation state */
    PH_FIRE,       /**< Server -> gun: firing context; gun -> server: chosen aim point */
    PH_KICK,       /**< Server -> client: the client is being disconnected (kick / duplicate id) */
    PH_STOP,       /**< Server -> anyone: the application is terminating */
    PH_MON         /**< Server -> monitor: full integrated snapshot of the tick */
};

/** \brief Role selector - the first byte of every connection */
#define ROLE_TANK 'T'
#define ROLE_GUN 'G'
#define ROLE_MONITOR 'M'
#define ROLE_CONTROL 'C'

/**
 * \brief Fixed-size message exchanged over TCP
 *
 * The whole structure is always transferred - simplicity wins over the
 * extra bytes, and the default socket buffers easily hold one message.
 * The structure contains no pointers, so it can be sent byte-by-byte
 * across machines with different architectures (all fields are ints).
 */
typedef struct
{
    int phase;         /**< Message phase (see enum above) */
    int id;            /**< Entity ID (1-based); 0 for monitors */
    pid_t pid;         /**< Attach: client PID; replies: server PID */
    int stop;          /**< Non-zero: the recipient must finish */
    long long tick;    /**< Current model tick */
    unsigned int seed; /**< Base PRNG seed (for a reproducible client RNG) */

    ShmField field;        /**< Snapshot of the field */
    Tank tanks[MAX_TANKS]; /**< Snapshot of all tanks */
    int tank_count;        /**< Number of tanks in the snapshot */

    Oreshnik gun;                            /**< Gun context (for PH_OBSERVE / PH_FIRE) */
    Oreshnik guns[MAX_GUNS];                 /**< All guns (for PH_MON snapshots) */
    int gun_count;                           /**< Number of guns */
    Projectile projectiles[MAX_PROJECTILES]; /**< Projectiles in flight (PH_MON) */

    SimStats stats; /**< Aggregated statistics (PH_MON) */
    int ax, ay;     /**< MOVE: chosen step (dx, dy); FIRE: aim point */
    int reason;     /**< Termination reason code (PH_MON / PH_STOP) */
    char note[64];  /**< Human-readable note (kick/stop reason) */
} Msg;

#endif /* PROTO_H */
