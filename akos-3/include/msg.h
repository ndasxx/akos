#ifndef MSG_H
#define MSG_H

/**
 * \file msg.h
 * \brief Message protocol of implementation D (independent processes,
 *        named semaphores + FIFO channels).
 *
 * There is NO shared memory in this implementation: the coordinator owns
 * the whole state, and the entities exchange fixed-size messages with it
 * through pairs of named FIFO channels (one channel per direction per
 * entity). Named POSIX semaphores synchronize the exchange:
 *  - "/akos3d_ready_<role>_<id>" — binary, posted by the coordinator after
 *    a message has been written to the entity channel;
 *  - "/akos3d_done" — counting, posted by an entity after its answer;
 *  - "/akos3d_print" — binary, serializes output lines.
 *
 * A message carries a snapshot of the field and of all tanks, so the
 * decision logic runs inside the entity process, while every change of
 * the authoritative state is applied (and validated) by the coordinator.
 */

#include <sys/types.h>
#include "state.h"

/**
 * \brief Message phases
 */
enum
{
    PH_ATTACH = 0, /**< Entity -> coordinator: "I am here" (carries entity PID) */
    PH_MOVE,       /**< Coordinator -> tank: move phase context; tank -> coordinator: chosen step */
    PH_OBSERVE,    /**< Coordinator -> gun: observation context; gun -> coordinator: new observation state */
    PH_FIRE,       /**< Coordinator -> gun: firing context; gun -> coordinator: chosen aim point */
    PH_STOP        /**< Coordinator -> entity: the application is terminating */
};

/**
 * \brief Fixed-size message exchanged through the FIFO channels
 */
typedef struct
{
    int phase;         /**< Message phase (see enum above) */
    int id;            /**< Entity ID (1-based) */
    pid_t pid;         /**< Attach: entity PID; any reply: coordinator PID */
    int stop;          /**< Non-zero: the entity must finish */
    long long tick;    /**< Current model tick */
    unsigned int seed; /**< Base PRNG seed (for a reproducible entity RNG) */

    ShmField field;        /**< Snapshot of the field */
    Tank tanks[MAX_TANKS]; /**< Snapshot of all tanks */
    int tank_count;        /**< Number of tanks in the snapshot */

    Oreshnik gun; /**< Gun context (for PH_OBSERVE / PH_FIRE) */

    int ax, ay; /**< MOVE: chosen step (dx, dy); FIRE: aim point */
} Msg;

/**
 * \brief Builds the FIFO path for an entity channel
 * \param buf   Output buffer
 * \param bufsz Buffer size
 * \param kind  "req" (entity->coordinator) or "rep" (coordinator->entity)
 * \param role  "tank" or "gun"
 * \param id    Entity ID (1-based)
 */
void fifo_path(char *buf, size_t bufsz, const char *kind, const char *role, int id);

/**
 * \brief Writes exactly n bytes to a descriptor (handles partial writes)
 * \param fd File descriptor
 * \param buf Buffer
 * \param n Number of bytes
 * \return 0 on success, -1 on error or EOF
 */
int write_full(int fd, const void *buf, size_t n);

/**
 * \brief Reads exactly n bytes from a descriptor
 * \param fd File descriptor
 * \param buf Buffer
 * \param n Number of bytes
 * \return 0 on success, -1 on error or EOF (e.g. the peer has terminated)
 */
int read_full(int fd, void *buf, size_t n);

#endif /* MSG_H */
