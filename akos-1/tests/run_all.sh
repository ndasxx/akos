#!/usr/bin/env bash
# tests/run_all.sh
# Прогон набора сценариев и проверка ключевых инвариантов.
# Запускается из корня проекта: bash tests/run_all.sh
# либо через make test.

set -u
BIN=./tank_sim
OUT=./tests/out
mkdir -p "$OUT"

rm -f "$OUT"/*.log

PASS=0
FAIL=0

if [ ! -x "$BIN" ]; then
    echo "ERROR: $BIN не найден или не исполняемый. Запустите 'make' сначала."
    exit 1
fi

check() {
    local name="$1"; shift
    local expect="$1"; shift
    local log="$OUT/$name.log"

    if ! "$BIN" "$@" > "$log" 2>&1; then
        echo "  [$name] FAIL: ненулевой код возврата"
        FAIL=$((FAIL+1))
        return 1
    fi

    if ! grep -q "$expect" "$log"; then
        echo "  [$name] FAIL: не найдено '$expect' в $log"
        FAIL=$((FAIL+1))
        return 1
    fi

    echo "  [$name] OK"
    PASS=$((PASS+1))
    return 0
}

check_greater_than_zero() {
    local name="$1"; shift
    local pattern="$1"; shift
    local log="$OUT/$name.log"

    if ! "$BIN" "$@" > "$log" 2>&1; then
        echo "  [$name] FAIL: ненулевой код возврата"
        FAIL=$((FAIL+1))
        return 1
    fi

    local value
    value=$(grep "$pattern" "$log" | grep -oE '[0-9]+' | tail -1)
    if [ -z "$value" ] || [ "$value" -eq 0 ]; then
        echo "  [$name] FAIL: $pattern равно 0 или не найдено"
        FAIL=$((FAIL+1))
        return 1
    fi

    echo "  [$name] OK (значение: $value)"
    PASS=$((PASS+1))
    return 0
}

echo "=== Прогон сценариев ==="

# ==================================================================
# Базовые тесты
# ==================================================================

# 1. Выстрел происходит (снаряд создаётся).
check shot_fired "ВЫСТРЕЛ" \
    --seed 42 --delay 0 --max-ticks 200 --no-field

# 2. Промахи: проверяем что их больше нуля (с seed=42 и accuracy=70% они будут).
check_greater_than_zero misses "Промахов" \
    --seed 42 --delay 0 --max-ticks 50 --no-field

# 3. Завершение по пределу тактов.
check max_ticks "достигнут предел тактов" \
    --seed 42 --delay 0 --max-ticks 3 --no-field

# 4. Лог-файл создаётся и содержит итоги.
LOG_CASE="$OUT/with_log.log"
"$BIN" --seed 7 --delay 0 --max-ticks 30 --no-field --log "$LOG_CASE" >/dev/null 2>&1
if [ -s "$LOG_CASE" ] && grep -q "ИТОГИ СИМУЛЯЦИИ" "$LOG_CASE"; then
    echo "  [logfile] OK"
    PASS=$((PASS+1))
else
    echo "  [logfile] FAIL: лог пуст или без итогов"
    FAIL=$((FAIL+1))
fi

# 5. Воспроизводимость: два прогона с одним seed идентичны.
"$BIN" --seed 42 --delay 0 --max-ticks 40 --no-field > "$OUT/seed42_a.log" 2>&1
"$BIN" --seed 42 --delay 0 --max-ticks 40 --no-field > "$OUT/seed42_b.log" 2>&1
if diff -q "$OUT/seed42_a.log" "$OUT/seed42_b.log" > /dev/null; then
    echo "  [reproducible] OK"
    PASS=$((PASS+1))
else
    echo "  [reproducible] FAIL: одинаковый seed дал разные результаты"
    FAIL=$((FAIL+1))
fi

# 6. Недетерминированность: разные seed дают разные результаты.
"$BIN" --seed 1 --delay 0 --max-ticks 40 --no-field > "$OUT/seed1.log" 2>&1
if ! diff -q "$OUT/seed1.log" "$OUT/seed42_a.log" > /dev/null; then
    echo "  [different_seeds] OK"
    PASS=$((PASS+1))
else
    echo "  [different_seeds] FAIL: разные seed дали одинаковый вывод"
    FAIL=$((FAIL+1))
fi

# 7. Справка.
if "$BIN" --help | grep -q "Usage:"; then
    echo "  [help] OK"
    PASS=$((PASS+1))
else
    echo "  [help] FAIL: нет строки Usage"
    FAIL=$((FAIL+1))
fi

# ==================================================================
# Тесты инвариантов и флагов
# ==================================================================

# 8. Флаг --no-field — поле НЕ печатается.
"$BIN" --seed 42 --delay 0 --max-ticks 10 --no-field > "$OUT/no_field.log" 2>&1
if ! grep -q "\[FIELD" "$OUT/no_field.log"; then
    echo "  [no_field] OK"
    PASS=$((PASS+1))
else
    echo "  [no_field] FAIL: поле печатается при --no-field"
    FAIL=$((FAIL+1))
fi

# 9. Флаг --no-field отсутствует — поле печатается).
"$BIN" --seed 42 --delay 0 --max-ticks 10 > "$OUT/with_field.log" 2>&1
if grep -q "\[FIELD" "$OUT/with_field.log"; then
    echo "  [with_field] OK"
    PASS=$((PASS+1))
else
    echo "  [with_field] FAIL: поле не печатается по умолчанию"
    FAIL=$((FAIL+1))
fi

# 10. Итоговая статистика всегда присутствует.
"$BIN" --seed 42 --delay 0 --max-ticks 10 --no-field > "$OUT/stats.log" 2>&1
if grep -q "ИТОГИ СИМУЛЯЦИИ" "$OUT/stats.log" && \
   grep -q "Орудия:" "$OUT/stats.log" && \
   grep -q "Танки:" "$OUT/stats.log"; then
    echo "  [stats_output] OK"
    PASS=$((PASS+1))
else
    echo "  [stats_output] FAIL: итоговая статистика неполная"
    FAIL=$((FAIL+1))
fi

echo
echo "=== Итог: пройдено $PASS, провалено $FAIL ==="
[ "$FAIL" -eq 0 ]