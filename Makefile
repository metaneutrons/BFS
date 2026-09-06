# BFS — Build system
#
# Targets:
#   make host-test   — build and run tests on host (macOS/Linux)
#   make amiga       — cross-compile Amiga handler (requires bebbo's gcc)
#   make clean

# ── Toolchains ──────────────────────────────────────────────
HOST_CC  = cc
AMIGA_CC = m68k-amigaos-gcc
GCOVR    = gcovr
GCOV     = gcov

# ── Flags ───────────────────────────────────────────────────
INCLUDES = -I include -I tests

HOST_CFLAGS  = -std=c99 -Wall -Wextra -Werror -g -O2 -pthread \
               $(INCLUDES) -DBFS_HOST=1 -D_POSIX_C_SOURCE=200809L
AMIGA_PREFIX = $(shell brew --prefix amiga-gcc 2>/dev/null || echo /opt/homebrew/opt/amiga-gcc)/m68k-amigaos
# The NDK declares STRPTR as unsigned char* in C, unlike standard C strings.
# Suppress only that header-boundary diagnostic and reject every other warning.
AMIGA_WARNINGS = -Wall -Wextra -Werror -Wno-pointer-sign
AMIGA_CFLAGS = -std=c99 $(AMIGA_WARNINGS) -O2 -m68020 -noixemul -fomit-frame-pointer \
               -Isrc/amiga $(INCLUDES) -DBFS_AMIGA=1 \
               -I$(AMIGA_PREFIX)/ndk-include

# ── Sources ─────────────────────────────────────────────────
CORE_SRC = $(wildcard src/core/*.c)
HOST_HEADERS = $(wildcard include/*.h tests/*.h)
CORE_SRC_AMIGA = $(filter-out src/core/crc32.c,$(CORE_SRC))
TEST_SRC = $(wildcard tests/test_*.c)
EMU_SRC  = tests/block_device_emu.c

# ── Build dirs ──────────────────────────────────────────────
BUILD_HOST  = build/host
BUILD_AMIGA = build/amiga

# ── Test binaries ───────────────────────────────────────────
TEST_BINS = $(patsubst tests/test_%.c,$(BUILD_HOST)/test_%,$(TEST_SRC))

# ── Phony targets ───────────────────────────────────────────
.PHONY: setup check repository-audit quality-gates shellcheck actionlint secrets-scan analyze \
	host-test coverage sanitize amiga amiga-stresstest clean tools stress-test bench release

setup:
	@command -v lefthook >/dev/null 2>&1 || { \
		echo "lefthook is required (brew install lefthook)" >&2; exit 1; }
	@command -v gitleaks >/dev/null 2>&1 || { \
		echo "gitleaks is required (brew install gitleaks)" >&2; exit 1; }
	@lefthook install

check: repository-audit quality-gates shellcheck actionlint secrets-scan analyze host-test

repository-audit:
	@tools/check-no-binaries.sh
	@tools/check-actions-pinned.sh

quality-gates:
	@tests/quality/test-gates.sh
	@python3 -m unittest discover -s tests/quality -p 'test_*.py' -v
	@lefthook validate

shellcheck:
	@shellcheck -x $$(git ls-files '*.sh')

actionlint:
	@actionlint

secrets-scan:
	@gitleaks git --redact --verbose .

analyze:
	@clang --analyze -Xanalyzer -analyzer-output=text \
		-std=c99 -Wall -Wextra -Werror -pthread $(INCLUDES) \
		-DBFS_HOST=1 -D_POSIX_C_SOURCE=200809L $(CORE_SRC) $(EMU_SRC)

host-test: $(TEST_BINS)
	@echo "=== Running tests ==="
	@fail=0; \
	for t in $(notdir $(TEST_BINS)); do \
		echo "--- $(BUILD_HOST)/$$t ---"; \
		(cd $(BUILD_HOST) && ./$$t) || fail=1; \
	done; \
	if [ $$fail -eq 0 ]; then echo "\n=== ALL TESTS PASSED ==="; \
	else echo "\n=== SOME TESTS FAILED ===" && exit 1; fi

coverage:
	@rm -rf build/coverage build/coverage.xml
	@$(MAKE) host-test BUILD_HOST=build/coverage \
		HOST_CFLAGS='-std=c99 -Wall -Wextra -Werror -g -O0 -pthread --coverage $(INCLUDES) -DBFS_HOST=1 -D_POSIX_C_SOURCE=200809L'
	@$(GCOVR) --root . --filter src/core --exclude-unreachable-branches \
		--gcov-executable $(GCOV) \
		--gcov-ignore-parse-errors=suspicious_hits.warn_once_per_file \
		--fail-under-line 85 --print-summary --xml-pretty --output build/coverage.xml \
		build/coverage

sanitize:
	@rm -rf build/sanitize
	@$(MAKE) host-test BUILD_HOST=build/sanitize \
		HOST_CFLAGS='-std=c99 -Wall -Wextra -Werror -g -O1 -pthread \
		-fno-omit-frame-pointer -fsanitize=address,undefined $(INCLUDES) \
		-DBFS_HOST=1 -D_POSIX_C_SOURCE=200809L'

tools: $(BUILD_HOST)/bfsfsck

$(BUILD_HOST)/bfsfsck: tools/bfsfsck.c $(CORE_SRC) $(EMU_SRC) $(HOST_HEADERS)
	@mkdir -p $(BUILD_HOST)
	$(HOST_CC) $(HOST_CFLAGS) -o $@ $< $(CORE_SRC) $(EMU_SRC)

$(BUILD_HOST)/test_%: tests/test_%.c $(CORE_SRC) $(EMU_SRC) $(HOST_HEADERS)
	@mkdir -p $(BUILD_HOST)
	$(HOST_CC) $(HOST_CFLAGS) -o $@ $< $(CORE_SRC) $(EMU_SRC)

$(BUILD_HOST)/test_fsck: $(BUILD_HOST)/bfsfsck

amiga:
	@mkdir -p $(BUILD_AMIGA)
	$(AMIGA_CC) $(AMIGA_CFLAGS) -o $(BUILD_AMIGA)/bfshandler \
		$(AMIGA_SRCS) \
		-nostdlib -L$(AMIGA_PREFIX)/libnix/lib -L$(AMIGA_PREFIX)/lib -lamiga -lgcc -lnix -s

amiga-stresstest:
	@mkdir -p $(BUILD_AMIGA)
	$(AMIGA_CC) $(AMIGA_WARNINGS) -noixemul -m68020 -O2 -I$(AMIGA_PREFIX)/ndk-include \
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
# Shared handler source list — the dev `amiga` target and `release` MUST build
# the same binary, so both use this. The hand-written 68k asm replaces the
# portable C crc32.c (hence CORE_SRC_AMIGA, which filters it out) and libnix's
# memcpy/memset on this slow CPU. memcpy_68k.s provides optimised _memcpy AND
# _memset, so memset_68k.s (which only defines _memset) is deliberately not
# assembled here — assembling both would duplicate _memset.
AMIGA_ASM_SRCS = src/amiga/startup.s src/amiga/crc32_68k.s src/amiga/memcpy_68k.s
AMIGA_SRCS = $(AMIGA_ASM_SRCS) src/amiga/handler.c src/amiga/amiga_bio.c $(CORE_SRC_AMIGA)
AMIGA_LDFLAGS = -nostdlib -L$(AMIGA_PREFIX)/libnix/lib -L$(AMIGA_PREFIX)/lib -lamiga -lgcc -lnix -s
AMIGA_BASE_FLAGS = -std=c99 $(AMIGA_WARNINGS) -Os -noixemul -fomit-frame-pointer \
                   -Isrc/amiga -I include -I tests -DBFS_AMIGA=1 -I$(AMIGA_PREFIX)/ndk-include
AMIGA_RELEASE_CPUS = 020 030 040 060 080
AMIGA_TOOL_FLAGS = -std=c99 $(AMIGA_WARNINGS) -Os -m68020 -noixemul -I$(AMIGA_PREFIX)/ndk-include
AMIGA_TOOL_LDFLAGS = -B$(AMIGA_PREFIX)/libnix/lib/ \
                     -L$(AMIGA_PREFIX)/libnix/lib -L$(AMIGA_PREFIX)/lib -lamiga -s
TOOL_SRCS_TEST = tools/bfs-test.c
TOOL_SRCS_FMT = tools/bfsformat.c
TOOL_SRCS_SNAP = tools/bfssnapshot.c

release:
	@mkdir -p build/release build/link-maps
	@python3 tools/release/build_identity.py begin
	@echo "Building release binaries..."
	@set -e; for cpu in $(AMIGA_RELEASE_CPUS); do \
		echo "  68$$cpu..."; \
		$(AMIGA_CC) $(AMIGA_BASE_FLAGS) -m68$$cpu -o build/release/bfshandler.$$cpu $(AMIGA_SRCS) $(AMIGA_LDFLAGS) \
			-Wl,-Map,build/link-maps/bfshandler.$$cpu.map; \
	done
	@echo "  Tools (68020)..."
	@$(AMIGA_CC) $(AMIGA_TOOL_FLAGS) \
		-o build/release/bfs-test $(TOOL_SRCS_TEST) $(AMIGA_TOOL_LDFLAGS) -Wl,-Map,build/link-maps/bfs-test.map
	@$(AMIGA_CC) $(AMIGA_TOOL_FLAGS) \
		-o build/release/bfsformat $(TOOL_SRCS_FMT) $(AMIGA_TOOL_LDFLAGS) -Wl,-Map,build/link-maps/bfsformat.map
	@$(AMIGA_CC) $(AMIGA_TOOL_FLAGS) \
		-o build/release/bfssnapshot $(TOOL_SRCS_SNAP) $(AMIGA_TOOL_LDFLAGS) -Wl,-Map,build/link-maps/bfssnapshot.map
	@cp build/release/bfshandler.020 build/release/bfshandler
	@python3 tools/release/build_identity.py finish
	@echo "Done. Binaries in build/release/"
	@ls -la build/release/

# ── Host tools ──────────────────────────────────────────────
$(BUILD_HOST)/mkbfs: tools/mkbfs.c $(CORE_SRC) $(EMU_SRC)
	@mkdir -p $(BUILD_HOST)
	$(HOST_CC) $(HOST_CFLAGS) -o $@ $^

# ── Amiga test binary ───────────────────────────────────────
amiga-test: amiga
	@mkdir -p $(BUILD_AMIGA)
	$(AMIGA_CC) $(AMIGA_TOOL_FLAGS) \
		-o $(BUILD_AMIGA)/bfs-test tools/bfs-test.c $(AMIGA_TOOL_LDFLAGS)

# ── CI integration test ─────────────────────────────────────
ci-test: amiga amiga-test $(BUILD_HOST)/mkbfs
	@emulator-test/ci-test.sh

# ── Emulator integration test ───────────────────────────────
emulator-test: ci-test

emulator-test-ci: ci-test

emulator-setup:
	@tools/install-aros-rom.sh build/emulator/aros

amiga-bench:
	@mkdir -p $(BUILD_AMIGA)
	$(AMIGA_CC) $(AMIGA_WARNINGS) -noixemul -m68020 -O2 \
		-I$(AMIGA_PREFIX)/ndk-include \
		-B$(AMIGA_PREFIX)/libnix/lib/ \
		-L$(AMIGA_PREFIX)/libnix/lib -L$(AMIGA_PREFIX)/lib \
		-o $(BUILD_AMIGA)/bfs-bench tools/bfs-bench.c -lamiga
