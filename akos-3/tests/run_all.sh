#!/usr/bin/env bash
# tests/run_all.sh

set -u
cd "$(dirname "$0")/.."
CLEAN=scripts/cleanup.sh

bash "$CLEAN" >/dev/null 2>&1

OUT=tests/out
mkdir -p "$OUT"
rm -f "$OUT"/*.log

PASS=0
FAIL=0

# Общий маленький сценарий: поле 10x10, 4 препятствия, 3 танка hp=3
# random_walk, 2 орудия ammo=20, дальность 7, перезарядка 2, задержка
# выстрела 2, точность 70%, seed=42. На нём все танки гарантированно
# уничтожаются (проверено).
SMALL="--w 10 --h 10 --obstacles 4 --tanks 3 --guns 2 --ammo 20 --max-ticks 60 --delay 1 --seed 42"

ok()  { echo "  [$1] OK"; PASS=$((PASS+1)); }
bad() { echo "  [$1] FAIL: $2"; FAIL=$((FAIL+1)); }

# ------------------------------------------------------------------
# Одна программа (реализации A и B)
# ------------------------------------------------------------------

check() {
    local name="$1" bin="$2" expect="$3"; shift 3
    local log="$OUT/$name.log"

    bash "$CLEAN" >/dev/null 2>&1

    if [ ! -x "$bin" ]; then
        bad "$name" "$bin не найден или не собран (запустите 'make')"
        return 1
    fi
    if ! timeout 60 "$bin" "$@" > "$log" 2>&1; then
        bad "$name" "ненулевой код возврата (см. $log)"
        return 1
    fi
    if ! grep -q "$expect" "$log"; then
        bad "$name" "не найдено '$expect' в $log"
        return 1
    fi
    ok "$name"
}

test_single() {
    local n="$1" bin="$2"
    check "${n}_help" "$bin" "Использование:" --help
    check "${n}_shot" "$bin" "ВЫСТРЕЛ" \
        --seed 42 --delay 0 --max-ticks 200
    check "${n}_max_ticks" "$bin" "достигнут предел тактов" \
        --seed 42 --delay 0 --max-ticks 3
    "$bin" --seed 1  --delay 0 --max-ticks 40 > "$OUT/${n}_seed1.log" 2>&1
    "$bin" --seed 42 --delay 0 --max-ticks 40 > "$OUT/${n}_seed42.log" 2>&1
    if ! diff -q <(sort "$OUT/${n}_seed1.log") <(sort "$OUT/${n}_seed42.log") > /dev/null; then
        ok "${n}_seeds"
    else
        bad "${n}_seeds" "разные seed дали одинаковый вывод"
    fi
}

sigint_single() {
    local n="$1" bin="$2"
    local log="$OUT/${n}_sigint.log"
    "$bin" --seed 7 --delay 200 --max-ticks 100000 > "$log" 2>&1 &
    local pid=$!
    sleep 2
    kill -INT "$pid"
    wait "$pid"
    if [ $? -eq 0 ] && grep -q "прервано пользователем" "$log" && \
       grep -q "Причина завершения" "$log"; then
        ok "${n}_sigint"
    else
        bad "${n}_sigint" "SIGINT не обработан корректно (см. $log)"
    fi
}

# ------------------------------------------------------------------
# Независимые программы (реализации C и D)
# ------------------------------------------------------------------

run_independent() {
    bash "$CLEAN" >/dev/null 2>&1
    local name="$1" coord_bin="$2" tank_bin="$3" gun_bin="$4" expect="$5"
    shift 5
    local log="$OUT/$name.log"

    for b in "$coord_bin" "$tank_bin" "$gun_bin"; do
        if [ ! -x "$b" ]; then
            bad "$name" "$b не найден или не собран (запустите 'make')"
            return 1
        fi
    done

    local nt=3 ng=2 a prev=""
    for a in "$@"; do
        case "$prev" in
            --tanks) nt=$a ;;
            --guns)  ng=$a ;;
        esac
        prev=$a
    done

    timeout 120 "$coord_bin" "$@" > "$log" 2>&1 &
    local coord_pid=$!
    sleep "${LAUNCH_DELAY:-0.5}"

    local ent_pids="" i
    for ((i = 1; i <= nt; i++)); do
        timeout 120 "$tank_bin" --id $i >> "$log" 2>&1 &
        ent_pids="$ent_pids $!"
    done
    for ((i = 1; i <= ng; i++)); do
        timeout 120 "$gun_bin" --id $i >> "$log" 2>&1 &
        ent_pids="$ent_pids $!"
    done

    local w=0
    while kill -0 "$coord_pid" 2>/dev/null && [ $w -lt 90 ]; do
        sleep 1; w=$((w+1))
    done
    if kill -0 "$coord_pid" 2>/dev/null; then
        kill -9 "$coord_pid" $ent_pids 2>/dev/null
        bad "$name" "координатор не завершился за 90 с"
        return 1
    fi
    wait "$coord_pid"
    local rc=$?
    if [ $rc -ne 0 ]; then
        bad "$name" "код возврата координатора $rc (см. $log)"
        return 1
    fi
    sleep 1
    for p in $ent_pids; do
        kill -9 "$p" 2>/dev/null
    done
    wait $ent_pids 2>/dev/null

    if ! grep -q "$expect" "$log"; then
        bad "$name" "не найдено '$expect' в $log"
        return 1
    fi
    ok "$name"
}

test_independent() {
    local n="$1" coord="$2" tank="$3" gun="$4"
    check "${n}_help" "$coord" "Использование:" --help
    run_independent "${n}_shot" "$coord" "$tank" "$gun" "ВЫСТРЕЛ" \
        --seed 42 --delay 0 --max-ticks 200
    run_independent "${n}_max_ticks" "$coord" "$tank" "$gun" "достигнут предел тактов" \
        --seed 42 --delay 0 --max-ticks 3
    run_independent "${n}_seed1" "$coord" "$tank" "$gun" "Причина завершения" \
        --seed 1 --delay 0 --max-ticks 40
    run_independent "${n}_seed42" "$coord" "$tank" "$gun" "Причина завершения" \
        --seed 42 --delay 0 --max-ticks 40
    if ! diff -q <(sort "$OUT/${n}_seed1.log") <(sort "$OUT/${n}_seed42.log") > /dev/null; then
        ok "${n}_seeds"
    else
        bad "${n}_seeds" "разные seed дали одинаковый вывод"
    fi
}

sigint_independent() {
    local n="$1" coord="$2" tank="$3" gun="$4"
    local log="$OUT/${n}_sigint.log"
    timeout 120 "$coord" --seed 7 --delay 200 --max-ticks 100000 > "$log" 2>&1 &
    local coord_pid=$!
    sleep 0.5
    local ent_pids="" i
    for ((i = 1; i <= 3; i++)); do
        timeout 120 "$tank" --id $i >> "$log" 2>&1 &
        ent_pids="$ent_pids $!"
    done
    for ((i = 1; i <= 2; i++)); do
        timeout 120 "$gun" --id $i >> "$log" 2>&1 &
        ent_pids="$ent_pids $!"
    done
    sleep 2
    kill -INT "$coord_pid"
    wait "$coord_pid"
    if [ $? -eq 0 ] && grep -q "прервано пользователем" "$log" && \
       grep -q "Причина завершения" "$log"; then
        ok "${n}_sigint"
    else
        bad "${n}_sigint" "SIGINT не обработан корректно (см. $log)"
    fi
    sleep 1
    for p in $ent_pids; do
        kill -9 "$p" 2>/dev/null
    done
    wait $ent_pids 2>/dev/null
}

# ------------------------------------------------------------------
# Общие проверки
# ------------------------------------------------------------------

summary() {
    awk '/^Орудия:/{f=1} f{print} /^Причина завершения/{exit}' "$1"
}

stats_ok() {
    local n="$1" log="$2"
    if [ -f "$log" ] && grep -q "Орудия:" "$log" && \
       grep -q "Танки:" "$log" && grep -q "Причина завершения" "$log"; then
        ok "${n}_stats"
    else
        bad "${n}_stats" "итоговая статистика неполная (см. $log)"
    fi
}

echo "=== Прогон тестов ==="

# ==================================================================
# Реализация A: fork + именованные семафоры + shm
# ==================================================================
test_single a ./sim_named
check a_named ./sim_named "все танки уничтожены" $SMALL
stats_ok a "$OUT/a_named.log"
sigint_single a ./sim_named
LOG_CASE="$OUT/a_logfile.txt"
bash "$CLEAN" >/dev/null 2>&1
./sim_named --seed 7 --delay 0 --max-ticks 30 --log "$LOG_CASE" > /dev/null 2>&1
if [ -s "$LOG_CASE" ] && grep -q "Причина завершения" "$LOG_CASE"; then
    ok "a_logfile"
else
    bad "a_logfile" "лог пуст или без итогов"
fi
bash "$CLEAN" >/dev/null 2>&1
if yes '' | head -n 30 | timeout 30 ./sim_named --interactive \
       > "$OUT/a_interactive.log" 2>&1 && \
   grep -q "Причина завершения" "$OUT/a_interactive.log"; then
    ok "a_interactive"
else
    bad "a_interactive" "интерактивный режим не завершился корректно"
fi

# ==================================================================
# Реализация B: fork + неименованные семафоры в shm
# ==================================================================
test_single b ./sim_unnamed
check b_unnamed ./sim_unnamed "все танки уничтожены" $SMALL
stats_ok b "$OUT/b_unnamed.log"
sigint_single b ./sim_unnamed
bash "$CLEAN" >/dev/null 2>&1
if yes '' | head -n 30 | timeout 30 ./sim_unnamed --interactive \
       > "$OUT/b_interactive.log" 2>&1 && \
   grep -q "Причина завершения" "$OUT/b_interactive.log"; then
    ok "b_interactive"
else
    bad "b_interactive" "интерактивный режим не завершился корректно"
fi

# ==================================================================
# Реализация C: независимые программы + именованные семафоры + shm
# ==================================================================
test_independent c ./coord ./ent_tank ./ent_gun
run_independent c_coord ./coord ./ent_tank ./ent_gun "все танки уничтожены" $SMALL
stats_ok c "$OUT/c_coord.log"
sigint_independent c ./coord ./ent_tank ./ent_gun
LAUNCH_DELAY=0 run_independent c_race ./coord ./ent_tank ./ent_gun \
    "Подключено: 5/5" $SMALL
LOG_CASE="$OUT/c_logfile.txt"
timeout 120 ./coord $SMALL --log "$LOG_CASE" > /dev/null 2>&1 &
CPID=$!
sleep 0.5
for i in 1 2 3; do timeout 120 ./ent_tank --id $i > /dev/null 2>&1 & done
for i in 1 2;   do timeout 120 ./ent_gun  --id $i > /dev/null 2>&1 & done
wait "$CPID"
if [ -s "$LOG_CASE" ] && grep -q "Причина завершения" "$LOG_CASE"; then
    ok "c_logfile"
else
    bad "c_logfile" "лог пуст или без итогов"
fi
sleep 1

# ==================================================================
# Реализация D: независимые программы + FIFO
# ==================================================================
test_independent d ./coord_fifo ./ent_tank_fifo ./ent_gun_fifo
run_independent d_coord ./coord_fifo ./ent_tank_fifo ./ent_gun_fifo "все танки уничтожены" $SMALL
stats_ok d "$OUT/d_coord.log"
sigint_independent d ./coord_fifo ./ent_tank_fifo ./ent_gun_fifo
LAUNCH_DELAY=0 run_independent d_race ./coord_fifo ./ent_tank_fifo ./ent_gun_fifo \
    "Подключено: 5/5" $SMALL

# ==================================================================
# Эквивалентность и очистка IPC
# ==================================================================

if [ -f "$OUT/a_named.log" ] && [ -f "$OUT/b_unnamed.log" ] && \
   [ -f "$OUT/c_coord.log" ] && [ -f "$OUT/d_coord.log" ]; then
    S_A=$(summary "$OUT/a_named.log")
    S_B=$(summary "$OUT/b_unnamed.log")
    S_C=$(summary "$OUT/c_coord.log")
    S_D=$(summary "$OUT/d_coord.log")
    if [ -n "$S_A" ] && [ "$S_A" = "$S_B" ] && [ "$S_A" = "$S_C" ] && [ "$S_A" = "$S_D" ]; then
        ok "equivalence"
    else
        bad "equivalence" "итоговая статистика реализаций не совпадает"
    fi
else
    bad "equivalence" "пропущен из-за провалов выше"
fi

sleep 1
LEFT=$(ls /dev/shm/sem.akos3* /dev/shm/akos3* /tmp/akos3d_* 2>/dev/null | wc -l)
if [ "$LEFT" -eq 0 ]; then
    ok "ipc_cleanup"
else
    bad "ipc_cleanup" "осталось объектов: $LEFT (см. scripts/cleanup.sh)"
fi

echo
echo "=== Итог: пройдено $PASS, провалено $FAIL ==="
[ "$FAIL" -eq 0 ]
