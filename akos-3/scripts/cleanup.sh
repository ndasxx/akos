#!/bin/bash
# Removes the IPC objects of all implementations
set -u

echo "Удаление именованных семафоров POSIX..."
for f in /dev/shm/sem.akos3* /dev/shm/akos3*; do
    [ -e "$f" ] && rm -f "$f" && echo "  удалён: $f"
done

echo "Удаление каналов FIFO..."
for f in /tmp/akos3d_*; do
    [ -e "$f" ] && rm -f "$f" && echo "  удалён: $f"
done

echo "Завершение оставшихся процессов..."
pkill -f '^(./)?(sim_named|sim_unnamed|coord|ent_tank|ent_gun|coord_fifo|ent_tank_fifo|ent_gun_fifo)( |$)' 2>/dev/null \
    && echo "  процессы завершены" || echo "  процессов не найдено"

echo "Готово."
