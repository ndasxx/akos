/**
 * \file shmutil.c
 * \brief Implementation of the shared memory helpers.
 */

#include "shmutil.h"
#include "semutil.h"
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

int shm_create(const char *name, size_t size, void **out)
{
    int fd = shm_open(name, O_CREAT | O_EXCL | O_RDWR, 0666);
    if (fd == -1)
    {
        perror("shm_open");
        fprintf(stderr, "Объект %s уже существует или недоступен. "
                        "Запустите scripts/cleanup.sh\n",
                name);
        return -1;
    }
    if (ftruncate(fd, (off_t)size) == -1)
    {
        perror("ftruncate");
        close(fd);
        shm_unlink(name);
        return -1;
    }
    void *p = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (p == MAP_FAILED)
    {
        perror("mmap");
        close(fd);
        shm_unlink(name);
        return -1;
    }
    close(fd);
    *out = p;
    return 0;
}

int shm_attach(const char *name, size_t size, void **out)
{
    int fd = shm_open(name, O_RDWR, 0666);
    if (fd == -1)
    {
        perror("shm_open");
        return -1;
    }
    void *p = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (p == MAP_FAILED)
    {
        perror("mmap");
        close(fd);
        return -1;
    }
    close(fd);
    *out = p;
    return 0;
}

int shm_attach_wait(const char *name, size_t size, int timeout_ms, void **out)
{
    int waited = 0;
    for (;;)
    {
        int fd = shm_open(name, O_RDWR, 0666);
        if (fd != -1)
        {
            void *p = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
            if (p == MAP_FAILED)
            {
                perror("mmap");
                close(fd);
                return -1;
            }
            close(fd);
            *out = p;
            return 0;
        }
        if (waited >= timeout_ms)
        {
            perror("shm_open");
            return -1;
        }
        sleep_ms(100);
        waited += 100;
    }
}

void shm_destroy(void *addr, size_t size, const char *name)
{
    if (addr && addr != MAP_FAILED)
        munmap(addr, size);
    if (shm_unlink(name) == -1)
        perror("shm_unlink");
}
