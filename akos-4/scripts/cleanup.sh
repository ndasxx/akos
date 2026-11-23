#!/bin/bash
# Stops the programs (in case a run was interrupted, e.g. kill -9).
set -u

echo "Завершение оставшихся процессов..."
pkill -f '^(./)?(server|tank_client|gun_client|monitor|control)( |$)' 2>/dev/null \
    && echo "  процессы завершены" || echo "  процессов не найдено"

echo "Готово."
