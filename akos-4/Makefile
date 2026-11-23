# Makefile for task #4 "Fire at moving tanks" - TCP client-server version.
#
#   make            - build everything
#   make run        - run the server with $(ARGS) (start the clients yourself)
#   make test       - run the test script (tests/run_all.sh)
#   make doc        - generate Doxygen documentation (Doxyfile)
#   make clean-all  - full cleanup (build + documentation + test logs)
#
# Binaries:
#   server        - the coordinator: authoritative state + TCP listener
#   tank_client   - tank entity (connects to the server)
#   gun_client    - gun entity (connects to the server)
#   monitor       - integrated view (reads the snapshots pushed by the server)
#   control       - operator console (list/kick/stop)

CC       = gcc
CFLAGS   = -std=c11 -Wall -Wextra -O2 -g
CPPFLAGS = -Iinclude -D_POSIX_C_SOURCE=200809L
LDFLAGS  =

# Common modules shared by all five programs
CORE := \
  src/field.c \
  src/rng.c \
  src/io.c \
  src/log.c \
  src/stats.c \
  src/signals.c \
  src/cfg.c \
  src/logic.c

CORE_OBJ := $(patsubst src/%.c,build/%.o,$(CORE))

BINS := server tank_client gun_client monitor control

BUILD_DIR := build
DOC_DIR   := documentation

all: $(BINS)

server: build/server.o $(CORE_OBJ) build/net.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

tank_client: build/tank_client.o build/logic.o build/field.o build/rng.o build/io.o build/signals.o build/net.o build/log.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

gun_client: build/gun_client.o build/logic.o build/field.o build/rng.o build/io.o build/signals.o build/net.o build/log.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

monitor: build/monitor.o build/io.o build/signals.o build/net.o build/log.o build/field.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

control: build/control.o build/io.o build/signals.o build/net.o build/log.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

build/%.o: src/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

ARGS ?= --seed 42 --delay 100 --max-ticks 200 --log tank_sim.log --port 7777
run: server
	./server $(ARGS)

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
