# BFS — Build system
#
# Targets:
#   make host-test   — build and run tests on host (macOS/Linux)
#   make amiga       — cross-compile Amiga handler (requires bebbo's gcc)
#   make clean

# ── Toolchains ──────────────────────────────────────────────
HOST_CC  = cc
AMIGA_CC = m68k-amigaos-gcc

# ── Flags ───────────────────────────────────────────────────
INCLUDES = -I include -I tests

HOST_CFLAGS  = -std=c99 -Wall -Wextra -Werror -g -O2 $(INCLUDES) -DBFS_HOST=1
AMIGA_PREFIX = $(shell brew --prefix amiga-gcc 2>/dev/null || echo /opt/homebrew/Cellar/amiga-gcc/2025.07.13)/m68k-amigaos
AMIGA_CFLAGS = -std=c99 -Wall -O2 -m68020 -noixemul -fomit-frame-pointer \
               -Isrc/amiga $(INCLUDES) -DBFS_AMIGA=1 \
               -I$(AMIGA_PREFIX)/ndk-include

# ── Sources ─────────────────────────────────────────────────
CORE_SRC = $(wildcard src/core/*.c)
CORE_SRC_AMIGA = $(filter-out src/core/crc32.c,$(CORE_SRC))
TEST_SRC = $(wildcard tests/test_*.c)
EMU_SRC  = tests/block_device_emu.c

# ── Build dirs ──────────────────────────────────────────────
BUILD_HOST  = build/host
BUILD_AMIGA = build/amiga

# ── Test binaries ───────────────────────────────────────────
TEST_BINS = $(patsubst tests/test_%.c,$(BUILD_HOST)/test_%,$(TEST_SRC))

# ── Phony targets ───────────────────────────────────────────
.PHONY: host-test amiga amiga-stresstest clean tools stress-test bench

host-test: $(TEST_BINS)
	@echo "=== Running tests ==="
	@fail=0; \
	for t in $(TEST_BINS); do \
		echo "--- $$t ---"; \
		$$t || fail=1; \
	done; \
	if [ $$fail -eq 0 ]; then echo "\n=== ALL TESTS PASSED ==="; \
	else echo "\n=== SOME TESTS FAILED ===" && exit 1; fi

tools: $(BUILD_HOST)/bfsfsck

$(BUILD_HOST)/bfsfsck: tools/bfsfsck.c $(CORE_SRC) $(EMU_SRC)
	@mkdir -p $(BUILD_HOST)
	$(HOST_CC) $(HOST_CFLAGS) -o $@ $< $(CORE_SRC) $(EMU_SRC)

$(BUILD_HOST)/test_%: tests/test_%.c $(CORE_SRC) $(EMU_SRC)
	@mkdir -p $(BUILD_HOST)
	$(HOST_CC) $(HOST_CFLAGS) -o $@ $< $(CORE_SRC) $(EMU_SRC)

amiga:
	@mkdir -p $(BUILD_AMIGA)
	$(AMIGA_CC) $(AMIGA_CFLAGS) -o $(BUILD_AMIGA)/bfshandler \
		src/amiga/startup.s \
		src/amiga/crc32_68k.s \
		src/amiga/memset_68k.s \
		src/amiga/handler.c \
		src/amiga/amiga_bio.c \
		$(CORE_SRC_AMIGA) \
		-nostdlib -L$(AMIGA_PREFIX)/libnix/lib -L$(AMIGA_PREFIX)/lib -lamiga -lgcc -lnix -s

amiga-stresstest:
	@mkdir -p $(BUILD_AMIGA)
	$(AMIGA_CC) -noixemul -m68020 -O2 -I$(AMIGA_PREFIX)/ndk-include \
		-B$(AMIGA_PREFIX)/libnix/lib/ \
		-L$(AMIGA_PREFIX)/libnix/lib -L$(AMIGA_PREFIX)/lib \
		-o $(BUILD_AMIGA)/bfs-stresstest tools/bfs-stresstest.c -lamiga

stress-test: amiga amiga-stresstest
	@chmod +x emulator-test/run-stress.sh
	@emulator-test/run-stress.sh

bench: $(BUILD_HOST)/bench_btree
	@$(BUILD_HOST)/bench_btree

$(BUILD_HOST)/bench_btree: tests/bench_btree.c $(CORE_SRC) $(EMU_SRC)
	@mkdir -p $(BUILD_HOST)
	$(HOST_CC) $(HOST_CFLAGS) -o $@ $< $(CORE_SRC) $(EMU_SRC)

clean:
	rm -rf build/ *.img

# ── Release builds (all CPU targets) ───────────────────────
AMIGA_SRCS = src/amiga/startup.s src/amiga/handler.c src/amiga/amiga_bio.c $(CORE_SRC)
AMIGA_LDFLAGS = -nostdlib -L$(AMIGA_PREFIX)/libnix/lib -L$(AMIGA_PREFIX)/lib -lamiga -lgcc -lnix -s
AMIGA_BASE_FLAGS = -std=c99 -Wall -Os -noixemul -fomit-frame-pointer \
                   -Isrc/amiga -I include -I tests -DBFS_AMIGA=1 -I$(AMIGA_PREFIX)/ndk-include
TOOL_SRCS_TEST = tools/bfs-test.c
TOOL_SRCS_FMT = tools/bfsformat.c
TOOL_SRCS_SNAP = tools/bfssnapshot.c

release:
	@mkdir -p build/release
	@echo "Building release binaries..."
	@for cpu in 020 030 040 060; do \
		echo "  68$$cpu..."; \
		$(AMIGA_CC) $(AMIGA_BASE_FLAGS) -m68$$cpu -o build/release/bfshandler.$$cpu $(AMIGA_SRCS) $(AMIGA_LDFLAGS); \
	done
	@echo "  Tools (68020)..."
	@$(AMIGA_CC) -std=c99 -Wall -Os -m68020 -I$(AMIGA_PREFIX)/ndk-include \
		-o build/release/bfs-test $(TOOL_SRCS_TEST) -s
	@$(AMIGA_CC) -std=c99 -Wall -Os -m68020 -I$(AMIGA_PREFIX)/ndk-include \
		-o build/release/bfsformat $(TOOL_SRCS_FMT) -s
	@$(AMIGA_CC) -std=c99 -Wall -Os -m68020 -I$(AMIGA_PREFIX)/ndk-include \
		-o build/release/bfssnapshot $(TOOL_SRCS_SNAP) -s
	@cp build/release/bfshandler.020 build/release/bfshandler
	@echo "Done. Binaries in build/release/"
	@ls -la build/release/

# ── Host tools ──────────────────────────────────────────────
$(BUILD_HOST)/mkbfs: tools/mkbfs.c $(CORE_SRC) $(EMU_SRC)
	@mkdir -p $(BUILD_HOST)
	$(HOST_CC) -std=c99 -O2 $(INCLUDES) -DBFS_HOST=1 -o $@ $^

# ── Amiga test binary ───────────────────────────────────────
amiga-test: amiga
	@mkdir -p $(BUILD_AMIGA)
	$(AMIGA_CC) -std=c99 -Wall -Os -m68020 -I/opt/homebrew/opt/amiga-gcc/m68k-amigaos/ndk-include \
		-o $(BUILD_AMIGA)/bfs-test tools/bfs-test.c -s

# ── CI integration test ─────────────────────────────────────
ci-test: amiga amiga-test $(BUILD_HOST)/mkbfs
	@emulator-test/ci-test.sh

# ── Emulator integration test ───────────────────────────────
emulator-test: amiga
	@chmod +x emulator-test/run.sh
	@emulator-test/run.sh

emulator-test-ci: amiga
	@chmod +x emulator-test/run.sh
	@emulator-test/run.sh --ci

emulator-setup: amiga
	@chmod +x emulator-test/run.sh
	@emulator-test/run.sh --setup-only

amiga-bench:
	@mkdir -p $(BUILD_AMIGA)
	$(AMIGA_CC) -noixemul -m68020 -O2 \
		-I$(AMIGA_PREFIX)/ndk-include \
		-B$(AMIGA_PREFIX)/libnix/lib/ \
		-L$(AMIGA_PREFIX)/libnix/lib -L$(AMIGA_PREFIX)/lib \
		-o $(BUILD_AMIGA)/bfs-bench tools/bfs-bench.c -lamiga
