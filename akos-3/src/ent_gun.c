/**
 * \file ent_gun.c
 * \brief Gun process of the independent implementation C.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "attach.h"
#include "proc.h"
#include "io.h"
#include "signals.h"
#include "semutil.h"

/** \brief Shared memory object of implementation C */
#define SHM_NAME "/akos3c_shm"
/** \brief Semaphore family prefix of implementation C */
#define NS_PREFIX "/akos3c"

int main(int argc, char **argv)
{
    io_init(STDOUT_FILENO);

    int id = 0;
    for (int i = 1; i + 1 < argc; ++i)
        if (!strcmp(argv[i], "--id"))
            id = atoi(argv[i + 1]);
    if (id < 1)
    {
        fprintf(stderr, "Использование: %s --id N\n", argv[0]);
        return 1;
    }

    if (sig_install() != 0)
    {
        fprintf(stderr, "Не удалось установить обработчики сигналов\n");
        return 1;
    }

    EntityAttach ea;
    if (entity_attach(SHM_NAME, NS_PREFIX, 'g', id, &ea) != 0)
        return 1;

    int rc = gun_proc_main(ea.st, ea.mutex, ea.done, ea.go, id - 1);

    entity_detach(&ea);
    return rc;
}
