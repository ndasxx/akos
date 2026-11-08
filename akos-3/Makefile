#   make            - build everything
#   make run        - run implementation A (sim_named) with $(ARGS)
#   make test       - run the test script (tests/run_all.sh)
#   make doc        - generate Doxygen documentation (Doxyfile)
#   make clean-all  - full cleanup (build + documentation + test logs)
#
# Binaries:
#   sim_named / sim_unnamed - one program, coordinator + fork'ed entities
#                             (A: named semaphores; B: unnamed in shm)
#   coord / ent_tank / ent_gun - independent processes (C: named sems + shm)
#   coord_fifo / ent_tank_fifo / ent_gun_fifo - independent processes
#                             (D: named sems + FIFO channels)

CC       = gcc
CFLAGS   = -std=c11 -Wall -Wextra -O2 -g -pthread
CPPFLAGS = -Iinclude -D_POSIX_C_SOURCE=200809L
LDFLAGS  = -pthread -lrt

CORE := \
  src/field.c \
  src/rng.c \
  src/io.c \
  src/log.c \
  src/stats.c \
  src/signals.c \
  src/cfg.c \
  src/logic.c \
  src/semutil.c \
  src/coord.c \
  src/proc.c

CORE_OBJ := $(patsubst src/%.c,build/%.o,$(CORE))

BINS := sim_named sim_unnamed coord ent_tank ent_gun coord_fifo ent_tank_fifo ent_gun_fifo

BUILD_DIR := build
DOC_DIR   := documentation

all: $(BINS)

sim_named: build/sim_named.o $(CORE_OBJ) build/shmutil.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

sim_unnamed: build/sim_unnamed.o $(CORE_OBJ) build/shmutil.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

coord: build/coord_shm.o $(CORE_OBJ) build/shmutil.o build/attach.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

ent_tank: build/ent_tank.o $(CORE_OBJ) build/shmutil.o build/attach.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

ent_gun: build/ent_gun.o $(CORE_OBJ) build/shmutil.o build/attach.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

coord_fifo: build/coord_fifo.o $(CORE_OBJ) build/msg.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

ent_tank_fifo: build/ent_tank_fifo.o $(CORE_OBJ) build/msg.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

ent_gun_fifo: build/ent_gun_fifo.o $(CORE_OBJ) build/msg.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

build/%.o: src/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

ARGS ?= --seed 42 --delay 100 --max-ticks 200 --log tank_sim.log
run: sim_named
	./sim_named $(ARGS)

test: $(BINS)
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
	rm -rf $(BUILD_DIR) $(BINS)
	@echo "Build artifacts removed"

test-clean:
	rm -rf tests/out
	@echo "Test logs removed"

clean-all: clean doc-clean test-clean
	rm -f tank_sim.log
	@echo "Full cleanup complete"

.PHONY: all run test doc doc-clean clean test-clean clean-all
