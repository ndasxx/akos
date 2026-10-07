CC       = gcc
CFLAGS   = -std=c11 -Wall -Wextra -O2 -g
CPPFLAGS = -Iinclude -D_POSIX_C_SOURCE=200809L
LDFLAGS  =

SRC := \
  src/main.c \
  src/event.c \
  src/field.c \
  src/tank.c \
  src/gun.c \
  src/projectile.c \
  src/sim.c \
  src/rng.c \
  src/io.c \
  src/log.c \
  src/stats.c \
  src/config.c \
  src/signals.c

OBJ := $(patsubst src/%.c,build/%.o,$(SRC))

BIN       := tank_sim
BUILD_DIR := build
DOC_DIR   := documentation

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

build/%.o: src/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)


ARGS ?= --seed 42 --delay 100 --max-ticks 200 --log tank_sim.log
run: $(BIN)
	./$(BIN) $(ARGS)

test: $(BIN)
	@test -x tests/run_all.sh || { echo "tests/run_all.sh not found or not executable"; exit 1; }
	./tests/run_all.sh

doc:
	@command -v doxygen >/dev/null 2>&1 || { echo "doxygen is not installed"; exit 1; }
	doxygen Doxyfile
	@echo "Документация сгенерирована: $(DOC_DIR)/html/index.html"

doc-clean:
	rm -rf $(DOC_DIR)
	@echo "Документация удалена"

clean:
	rm -rf $(BUILD_DIR) $(BIN)
	@echo "Build artifacts removed"

test-clean:
	rm -rf tests/out
	@echo "Test logs removed"

clean-all: clean doc-clean test-clean
	rm -f tank_sim.log
	@echo "Full cleanup complete"

.PHONY: all run test doc doc-clean clean clean-all