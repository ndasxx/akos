#!/usr/bin/env bash
# tests/run_all.sh - прогон тестов
# подробные логи - в tests/out/*.log.
#
# Проверяется:
#   help (всех пяти программ) - справка печатается;
#   scenario          - полный сценарий: штатное завершение, статистика,
#                       все клиенты корректно завершились по PH_STOP;
#   monitor           - монитор получает комплексные снимки (поле, танки,
#                       орудия, статистика);
#   two_monitors      - два монитора одновременно;
#   monitor_rejoin    - монитор убит (kill -9), новый подключился в ходу,
#                       сервер продолжает работу;
#   kick_reconnect    - control отключает танк #2, танк переподключается;
#   kill_tank         - kill -9 танка: сервер продолжает работу;
#   stop_graceful     - control stop: сервер и все клиенты завершаются;
#   sigint_server     - Ctrl+C на сервере: корректный останов всех;
#   max_ticks         - завершение по пределу тактов;
#   seeds             - разные seed дают разные результаты;
#   repeatable        - повторный прогон даёт тот же результат;
#   wrong_id          - некорректный --id отклоняется сервером.

set -u
cd "$(dirname "$0")/.."
CLEAN=scripts/cleanup.sh

bash "$CLEAN" >/dev/null 2>&1

OUT=tests/out
mkdir -p "$OUT"
rm -f "$OUT"/*.log "$OUT"/*.pid "$OUT"/*_pids

PASS=0
FAIL=0
PORT=${BASE_PORT:-48300}

next_port() { PORT=$((PORT + 1)); echo $PORT; }

ok()  { PASS=$((PASS + 1)); printf '  [%s] OK\n' "$1"; }
bad() { FAIL=$((FAIL + 1)); printf '  [%s] FAIL: %s\n' "$1" "$2"; }

# Общий маленький сценарий: поле 10x10, 4 препятствия, 3 танка hp=3
# random_walk, 2 орудия ammo=20, дальность 7, перезарядка 2, задержка
# выстрела 2, точность 70%, seed=42. На нём все танки уничтожаются.
SMALL="--w 10 --h 10 --obstacles 4 --tanks 3 --guns 2 --ammo 20 --max-ticks 60 --delay 1 --seed 42"

# Длинный сценарий для проверок в динамике (kick/stop/kill).
LONG="--w 10 --h 10 --obstacles 4 --tanks 3 --guns 2 --ammo 10 --reload 5 --tank-hp 30 --damage-direct 1 --damage-splash 0 --max-ticks 200 --delay 200 --seed 42"

# ------------------------------------------------------------------
# Запуск типового состава: сервер + nt танков + ng орудий (+ опции)
# Использование: start_farm ИМЯ ПОРТ ПАРАМЕТРЫ_СЕРВЕРА [nt [ng]]
# ------------------------------------------------------------------
start_farm() {
    local name="$1" port="$2" args="$3" nt="${4:-3}" ng="${5:-2}"
    ./server $args --port "$port" > "$OUT/${name}_server.log" 2>&1 &
    echo $! > "$OUT/${name}_server.pid"
    sleep 0.3
    local i
    for ((i = 1; i <= nt; i++)); do
        ./tank_client --id "$i" --port "$port" > "$OUT/${name}_tank$i.log" 2>&1 &
        echo $! >> "$OUT/${name}_pids"
    done
    for ((i = 1; i <= ng; i++)); do
        ./gun_client --id "$i" --port "$port" > "$OUT/${name}_gun$i.log" 2>&1 &
        echo $! >> "$OUT/${name}_pids"
    done
}

wait_server() {
    local name="$1" timeout="${2:-60}"
    local pid
    pid=$(cat "$OUT/${name}_server.pid")
    local w=0
    while kill -0 "$pid" 2>/dev/null && [ "$w" -lt "$((timeout * 10))" ]; do
        sleep 0.1
        w=$((w + 1))
    done
    if kill -0 "$pid" 2>/dev/null; then
        return 1
    fi
    wait "$pid" 2>/dev/null
    return $?
}

# Все процессы завершились (код 0)?
farm_gone() {
    local name="$1"
    local pid
    for f in "$OUT/${name}_server.pid" "$OUT/${name}_pids"; do
        [ -f "$f" ] || continue
        while read -r pid; do
            kill -0 "$pid" 2>/dev/null && return 1
        done < "$f"
    done
    return 0
}

check() {
    local name="$1" bin="$2" expect="$3"
    shift 3

    bash "$CLEAN" >/dev/null 2>&1
    local log="$OUT/${name}.log"
    if ! timeout 10 "$bin" "$@" > "$log" 2>&1; then
        bad "$name" "ненулевой код возврата (см. $log)"
        return 1
    fi
    if ! grep -q "$expect" "$log"; then
        bad "$name" "не найдено '$expect' в $log"
        return 1
    fi
    ok "$name"
}

# ------------------------------------------------------------------
# 1. Справки
# ------------------------------------------------------------------
check server_help ./server "Использование:" --help
check tank_help ./tank_client "Использование:" --help
check gun_help ./gun_client "Использование:" --help
check monitor_help ./monitor "Использование:" --help
check control_help ./control "Использование:" --help

# ------------------------------------------------------------------
# 2. Полный сценарий: штатное завершение и корректный выход клиентов
# ------------------------------------------------------------------
{
    name=scenario
    bash "$CLEAN" >/dev/null 2>&1
    p=$(next_port)
    start_farm "$name" "$p" "$SMALL"
    if wait_server "$name" 60 && farm_gone "$name"; then
        if grep -q "все танки уничтожены" "$OUT/${name}_server.log" && \
           grep -q "Орудия:" "$OUT/${name}_server.log" && \
           grep -q "Танки:" "$OUT/${name}_server.log" && \
           grep -q "Причина завершения" "$OUT/${name}_server.log" && \
           grep -q "ВЫСТРЕЛ" "$OUT/${name}_server.log" && \
           grep -q "сервер завершил работу" "$OUT/${name}_tank1.log" && \
           grep -q "сервер завершил работу" "$OUT/${name}_gun2.log"; then
            ok scenario
        else
            bad scenario "ожидаемые строки не найдены в логах $OUT/${name}_*"
        fi
    else
        bad scenario "сервер не завершился или клиенты остались (см. $OUT/${name}_*)"
        bash "$CLEAN" >/dev/null 2>&1
    fi
}

# ------------------------------------------------------------------
# 3. Монитор: комплексный снимок
# ------------------------------------------------------------------
{
    name=monitor
    bash "$CLEAN" >/dev/null 2>&1
    p=$(next_port)
    start_farm "$name" "$p" "--w 10 --h 10 --obstacles 4 --tanks 3 --guns 2 --ammo 20 --max-ticks 60 --delay 1000 --seed 42"
    ./monitor --port "$p" > "$OUT/${name}_mon.log" 2>&1 &
    echo $! >> "$OUT/${name}_pids"
    if wait_server "$name" 60 && \
       grep -q "===== такт" "$OUT/${name}_mon.log" && \
       grep -q "Орудия:" "$OUT/${name}_mon.log" && \
       grep -q "Танки:" "$OUT/${name}_mon.log" && \
       grep -q "Статистика:" "$OUT/${name}_mon.log" && \
       grep -q "УНИЧТОЖЕН" "$OUT/${name}_mon.log"; then
        ok monitor
    else
        bad monitor "снимки монитора неполные (см. $OUT/${name}_mon.log)"
        bash "$CLEAN" >/dev/null 2>&1
    fi
}

# ------------------------------------------------------------------
# 4. Два монитора одновременно
# ------------------------------------------------------------------
{
    name=two_monitors
    bash "$CLEAN" >/dev/null 2>&1
    p=$(next_port)
    start_farm "$name" "$p" "$LONG"
    ./monitor --port "$p" > "$OUT/${name}_m1.log" 2>&1 &
    M1=$!
    sleep 1
    ./monitor --port "$p" > "$OUT/${name}_m2.log" 2>&1 &
    M2=$!
    echo "$M1" >> "$OUT/${name}_pids"
    echo "$M2" >> "$OUT/${name}_pids"
    if wait_server "$name" 90 && \
       [ "$(grep -ac '=====' "$OUT/${name}_m1.log")" -ge 2 ] && \
       [ "$(grep -ac '=====' "$OUT/${name}_m2.log")" -ge 2 ]; then
        ok two_monitors
    else
        bad two_monitors "мониторы не получали снимки (см. $OUT/${name}_m*.log)"
        bash "$CLEAN" >/dev/null 2>&1
    fi
}

# ------------------------------------------------------------------
# 5. Монитор убит (kill -9) - сервер работает, новый монитор подключается
# ------------------------------------------------------------------
{
    name=monitor_rejoin
    bash "$CLEAN" >/dev/null 2>&1
    p=$(next_port)
    start_farm "$name" "$p" "$LONG"
    ./monitor --port "$p" > "$OUT/${name}_m1.log" 2>&1 &
    M1=$!
    sleep 1.5
    kill -9 "$M1" 2>/dev/null
    disown "$M1" 2>/dev/null
    ./monitor --port "$p" > "$OUT/${name}_m2.log" 2>&1 &
    M2=$!
    echo "$M2" >> "$OUT/${name}_pids"
    if wait_server "$name" 90 && \
       grep -q "монитор (fd" "$OUT/${name}_server.log" && \
       [ "$(grep -ac '=====' "$OUT/${name}_m2.log")" -ge 2 ]; then
        ok monitor_rejoin
    else
        bad monitor_rejoin "сервер не пережил гибель монитора (см. $OUT/${name}_*)"
        bash "$CLEAN" >/dev/null 2>&1
    fi
}

# ------------------------------------------------------------------
# 6. control: kick tank 2 + переподключение танка
# ------------------------------------------------------------------
{
    name=kick_reconnect
    bash "$CLEAN" >/dev/null 2>&1
    p=$(next_port)
    start_farm "$name" "$p" "$LONG"
    sleep 2
    { printf 'kick tank 2\n'; sleep 1; } | ./control --port "$p" \
        > "$OUT/${name}_ctl.log" 2>&1
    sleep 1.5
    if ! grep -q "отключён сервером" "$OUT/${name}_tank2.log"; then
        bad kick_reconnect "танк #2 не был отключён (см. $OUT/${name}_tank2.log)"
        bash "$CLEAN" >/dev/null 2>&1
    else
        ./tank_client --id 2 --port "$p" > "$OUT/${name}_tank2b.log" 2>&1 &
        echo $! >> "$OUT/${name}_pids"
        if wait_server "$name" 90 && \
           grep -q "подключено (такт" "$OUT/${name}_tank2b.log" && \
           grep -q "Причина завершения" "$OUT/${name}_server.log"; then
            ok kick_reconnect
        else
            bad kick_reconnect "переподключение не удалось (см. $OUT/${name}_*)"
            bash "$CLEAN" >/dev/null 2>&1
        fi
    fi
}

# ------------------------------------------------------------------
# 7. kill -9 танка: сервер продолжает работу
# ------------------------------------------------------------------
{
    name=kill_tank
    bash "$CLEAN" >/dev/null 2>&1
    p=$(next_port)
    start_farm "$name" "$p" "$LONG"
    sleep 2
    T2PID=$(sed -n 2p "$OUT/${name}_pids")
    kill -9 "$T2PID" 2>/dev/null
    disown "$T2PID" 2>/dev/null
    if wait_server "$name" 90 && \
       grep -q "отключился — его фазы пропускаются" "$OUT/${name}_server.log" && \
       grep -q "Причина завершения" "$OUT/${name}_server.log"; then
        ok kill_tank
    else
        bad kill_tank "сервер не пережил гибель танка (см. $OUT/${name}_server.log)"
        bash "$CLEAN" >/dev/null 2>&1
    fi
}

# ------------------------------------------------------------------
# 8. control stop: корректное завершение сервера и всех клиентов
# ------------------------------------------------------------------
{
    name=stop_graceful
    bash "$CLEAN" >/dev/null 2>&1
    p=$(next_port)
    start_farm "$name" "$p" "$LONG"
    ./monitor --port "$p" > "$OUT/${name}_mon.log" 2>&1 &
    echo $! >> "$OUT/${name}_pids"
    sleep 2
    { printf 'stop\n'; sleep 1; } | ./control --port "$p" \
        > "$OUT/${name}_ctl.log" 2>&1
    if wait_server "$name" 30 && farm_gone "$name" && \
       grep -q "прервано пользователем" "$OUT/${name}_server.log" && \
       grep -q "сервер завершил работу" "$OUT/${name}_tank1.log" && \
       grep -q "сервер завершил работу" "$OUT/${name}_gun1.log" && \
       grep -q "сервер завершил работу" "$OUT/${name}_mon.log"; then
        ok stop_graceful
    else
        bad stop_graceful "не все клиенты завершились (см. $OUT/${name}_*)"
        bash "$CLEAN" >/dev/null 2>&1
    fi
}

# ------------------------------------------------------------------
# 9. SIGINT серверу: корректный останов всех
# ------------------------------------------------------------------
{
    name=sigint_server
    bash "$CLEAN" >/dev/null 2>&1
    p=$(next_port)
    start_farm "$name" "$p" "$LONG"
    ./monitor --port "$p" > "$OUT/${name}_mon.log" 2>&1 &
    echo $! >> "$OUT/${name}_pids"
    sleep 3
    kill -INT "$(cat "$OUT/${name}_server.pid")"
    if wait_server "$name" 30 && farm_gone "$name" && \
       grep -q "прервано пользователем" "$OUT/${name}_server.log" && \
       grep -q "сервер завершил работу" "$OUT/${name}_tank2.log"; then
        ok sigint_server
    else
        bad sigint_server "останов по SIGINT некорректен (см. $OUT/${name}_*)"
        bash "$CLEAN" >/dev/null 2>&1
    fi
}

# ------------------------------------------------------------------
# 10. Завершение по пределу тактов
# ------------------------------------------------------------------
{
    name=max_ticks
    bash "$CLEAN" >/dev/null 2>&1
    p=$(next_port)
    start_farm "$name" "$p" "--w 8 --h 8 --obstacles 2 --tanks 2 --guns 1 --ammo 5 --tank-hp 50 --max-ticks 3 --delay 1 --seed 42"
    if wait_server "$name" 60 && \
       grep -q "достигнут предел тактов" "$OUT/${name}_server.log"; then
        ok max_ticks
    else
        bad max_ticks "предел тактов не сработал (см. $OUT/${name}_server.log)"
        bash "$CLEAN" >/dev/null 2>&1
    fi
}

# ------------------------------------------------------------------
# 11. Разные seed - разные результаты
# ------------------------------------------------------------------
{
    name=seeds
    bash "$CLEAN" >/dev/null 2>&1
    p1=$(next_port)
    start_farm "${name}_a" "$p1" "--w 10 --h 10 --obstacles 4 --tanks 3 --guns 2 --ammo 20 --max-ticks 60 --delay 1 --seed 1"
    wait_server "${name}_a" 60
    bash "$CLEAN" >/dev/null 2>&1
    p2=$(next_port)
    start_farm "${name}_b" "$p2" "--w 10 --h 10 --obstacles 4 --tanks 3 --guns 2 --ammo 20 --max-ticks 60 --delay 1 --seed 42"
    wait_server "${name}_b" 60
    s1=$(grep "Выстрелов всего" "$OUT/${name}_a_server.log" || true)
    s2=$(grep "Выстрелов всего" "$OUT/${name}_b_server.log" || true)
    if [ -n "$s1" ] && [ -n "$s2" ] && [ "$s1" != "$s2" ]; then
        ok seeds
    else
        bad seeds "результаты совпали: '$s1' vs '$s2'"
        bash "$CLEAN" >/dev/null 2>&1
    fi
}

# ------------------------------------------------------------------
# 12. Повторяемость: одинаковый seed -> идентичная статистика
# ------------------------------------------------------------------
{
    name=repeatable
    bash "$CLEAN" >/dev/null 2>&1
    p1=$(next_port)
    start_farm "${name}_a" "$p1" "$SMALL"
    wait_server "${name}_a" 60
    bash "$CLEAN" >/dev/null 2>&1
    p2=$(next_port)
    start_farm "${name}_b" "$p2" "$SMALL"
    wait_server "${name}_b" 60
    extract() {
        awk '/^Орудия:/{f=1} f{print} /^Причина завершения/{exit}' \
            "$OUT/${name}_${1}_server.log"
    }
    if [ "$(extract a)" = "$(extract b)" ] && [ -n "$(extract a)" ]; then
        ok repeatable
    else
        bad repeatable "повторный прогон отличается (см. $OUT/${name}_*_server.log)"
    fi
}

# ------------------------------------------------------------------
# 13. Некорректный --id отклоняется сервером
# ------------------------------------------------------------------
{
    name=wrong_id
    bash "$CLEAN" >/dev/null 2>&1
    p=$(next_port)
    ./server $SMALL --port "$p" > "$OUT/${name}_server.log" 2>&1 &
    echo $! > "$OUT/${name}_server.pid"
    sleep 0.3
    ./tank_client --id 99 --port "$p" > "$OUT/${name}_tank.log" 2>&1
    rc=$?
    if [ "$rc" -ne 0 ] && \
       grep -q "подключение отклонено" "$OUT/${name}_tank.log" && \
       grep -q "отклонено подключение" "$OUT/${name}_server.log"; then
        ok wrong_id
    else
        bad wrong_id "некорректный id не отклонён (rc=$rc, см. $OUT/${name}_*)"
    fi
    bash "$CLEAN" >/dev/null 2>&1
}

# ------------------------------------------------------------------
bash "$CLEAN" >/dev/null 2>&1

echo ""
echo "=== Итог: пройдено $PASS, провалено $FAIL ==="
[ "$FAIL" -eq 0 ]
