# BFS — Build system
#
# Targets:
#   make host-test   — build and run tests on host (macOS/Linux)
#   make amiga       — cross-compile Amiga handler (requires bebbo's gcc)
#   make clean

# ── Toolchains ──────────────────────────────────────────────
HOST_CC  = cc
HOST_AR  = ar
AMIGA_CC = m68k-amigaos-gcc
GCOVR    = gcovr
GCOV     = gcov
BFS_VERSION = $(strip $(shell cat version.txt))

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
HOST_SRC = $(wildcard src/host/*.c)
CORE_HEADERS = $(wildcard include/*.h)
HOST_HEADERS = $(CORE_HEADERS) $(wildcard tests/*.h)
CORE_SRC_AMIGA = $(filter-out src/core/crc32.c,$(CORE_SRC))
TEST_SRC = $(wildcard tests/test_*.c)
EMU_SRC  = tests/block_device_emu.c

# ── Build dirs ──────────────────────────────────────────────
BUILD_HOST  = build/host
BUILD_AMIGA = build/amiga
HOST_CORE_OBJS = $(patsubst src/core/%.c,$(BUILD_HOST)/obj/core/%.o,$(CORE_SRC))
HOST_POSIX_OBJ = $(BUILD_HOST)/obj/host/posix_bio.o
HOST_LIB = $(BUILD_HOST)/libbfs.a
FUSE_CFLAGS = $(shell pkg-config --cflags fuse3 2>/dev/null)
FUSE_LIBS = $(shell pkg-config --libs fuse3 2>/dev/null)
BFS_WITH_FUSE ?= $(shell pkg-config --exists fuse3 && echo 1)
BFS_FUSE_SRC =
BFS_FUSE_FLAGS =
ifeq ($(BFS_WITH_FUSE),1)
BFS_FUSE_SRC = src/fuse/bfs_fuse.c
BFS_FUSE_FLAGS = -DBFS_FUSE_ENABLED $(FUSE_CFLAGS)
endif
CONFORMANCE_CORE = $(BUILD_HOST)/bfs-conformance-core
CONFORMANCE_POSIX = $(BUILD_HOST)/bfs-conformance-posix
CONFORMANCE_FIXTURE = $(BUILD_HOST)/conformance-fixture-writer

# ── Test binaries ───────────────────────────────────────────
TEST_BINS = $(patsubst tests/test_%.c,$(BUILD_HOST)/test_%,$(TEST_SRC))

# ── Phony targets ───────────────────────────────────────────
.PHONY: setup check repository-audit quality-gates shellcheck actionlint secrets-scan analyze \
	host-test coverage sanitize amiga amiga-stresstest clean tools stress-test bench release \
	conformance conformance-test linux-qualification-fast linux-qualification-soak \
	linux-qualification-soak-preflight linux-qualification-soak-verify qualification-tests

.PHONY: fuse

fuse:
	@pkg-config --exists fuse3 || { echo "libfuse3 development files are required" >&2; exit 1; }
	@$(MAKE) --always-make BFS_WITH_FUSE=1 $(BUILD_HOST)/bfs

.PHONY: fuse-test

fuse-test: fuse conformance $(CONFORMANCE_FIXTURE)
	@test -c /dev/fuse || { echo "/dev/fuse is required for FUSE qualification" >&2; exit 1; }
	@command -v fusermount3 >/dev/null 2>&1 || { echo "fusermount3 is required" >&2; exit 1; }
	@python3 tests/fuse/test_fuse_mount.py

qualification-tests:
	@python3 -m unittest discover -s tests/qualification -p 'test_*.py' -v

linux-qualification-fast: fuse conformance tools $(CONFORMANCE_FIXTURE) qualification-tests
	@test -c /dev/fuse || { echo "/dev/fuse is required for M7 qualification" >&2; exit 1; }
	@command -v fusermount3 >/dev/null 2>&1 || { echo "fusermount3 is required" >&2; exit 1; }
	@python3 tests/qualification/linux_qualification.py \
		--output build/linux-qualification/fast.json

linux-qualification-soak: fuse conformance tools $(CONFORMANCE_FIXTURE) qualification-tests
	@test -n "$(APPROVAL_REFERENCE)" || { echo "APPROVAL_REFERENCE is required" >&2; exit 2; }
	@test -n "$(OUTPUT)" || { echo "OUTPUT is required" >&2; exit 2; }
	@test -c /dev/fuse || { echo "/dev/fuse is required for M7 soak" >&2; exit 1; }
	@command -v fusermount3 >/dev/null 2>&1 || { echo "fusermount3 is required" >&2; exit 1; }
	@python3 tests/qualification/fuse_soak.py --output "$(OUTPUT)" \
		--approval-reference "$(APPROVAL_REFERENCE)" \
		$(if $(IMAGE_DIRECTORY),--image-directory "$(IMAGE_DIRECTORY)") $(SOAK_ARGS)
	@$(MAKE) linux-qualification-soak-verify OUTPUT="$(OUTPUT)"

linux-qualification-soak-preflight: fuse conformance tools $(CONFORMANCE_FIXTURE) qualification-tests
	@rm -rf build/linux-qualification/soak-preflight
	@python3 tests/qualification/fuse_soak.py \
		--output build/linux-qualification/soak-preflight \
		--preflight --duration-seconds 30
	@$(MAKE) linux-qualification-soak-verify OUTPUT=build/linux-qualification/soak-preflight

linux-qualification-soak-verify:
	@test -n "$(OUTPUT)" || { echo "OUTPUT is required" >&2; exit 2; }
	@python3 tests/qualification/verify_fuse_soak.py --output "$(OUTPUT)"

.PHONY: fuse-analyze

fuse-analyze:
	@pkg-config --exists fuse3 || { echo "libfuse3 development files are required" >&2; exit 1; }
	@clang --analyze -Xanalyzer -analyzer-output=text \
		-std=c99 -Wall -Wextra -Werror -pthread $(INCLUDES) \
		-DBFS_HOST=1 -D_POSIX_C_SOURCE=200809L $(FUSE_CFLAGS) \
		$(CORE_SRC) $(HOST_SRC) src/fuse/bfs_fuse.c

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
		-DBFS_HOST=1 -D_POSIX_C_SOURCE=200809L $(CORE_SRC) $(HOST_SRC) \
		tools/bfs-conformance-core.c tools/bfs-conformance-posix.c $(EMU_SRC)

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

tools: $(BUILD_HOST)/bfs

conformance: $(CONFORMANCE_CORE) $(CONFORMANCE_POSIX)

conformance-test: conformance tools $(CONFORMANCE_FIXTURE)

	@python3 -m unittest discover -s tests/conformance -p 'test_*.py' -v

$(CONFORMANCE_CORE): tools/bfs-conformance-core.c $(HOST_LIB) $(HOST_POSIX_OBJ) $(CORE_HEADERS)
	@mkdir -p $(BUILD_HOST)
	$(HOST_CC) $(HOST_CFLAGS) -o $@ $< $(HOST_POSIX_OBJ) $(HOST_LIB)

$(CONFORMANCE_POSIX): tools/bfs-conformance-posix.c
	@mkdir -p $(BUILD_HOST)
	$(HOST_CC) $(HOST_CFLAGS) -o $@ $<

$(CONFORMANCE_FIXTURE): tests/conformance/fixture_writer.c $(HOST_LIB) $(HOST_POSIX_OBJ) $(CORE_HEADERS)
	@mkdir -p $(BUILD_HOST)
	$(HOST_CC) $(HOST_CFLAGS) -o $@ $< $(HOST_POSIX_OBJ) $(HOST_LIB)

$(BUILD_HOST)/obj/core/%.o: src/core/%.c $(CORE_HEADERS)
	@mkdir -p $(dir $@)
	$(HOST_CC) $(HOST_CFLAGS) -c -o $@ $<

$(HOST_POSIX_OBJ): src/host/posix_bio.c $(CORE_HEADERS)
	@mkdir -p $(dir $@)
	$(HOST_CC) $(HOST_CFLAGS) -c -o $@ $<

$(HOST_LIB): $(HOST_CORE_OBJS)
	@mkdir -p $(dir $@)
	$(HOST_AR) rcs $@ $^

$(BUILD_HOST)/bfs: tools/bfs_host.c tools/bfs_host_common.c tools/bfs_host_common.h $(BFS_FUSE_SRC) \
		$(HOST_LIB) $(HOST_POSIX_OBJ) $(CORE_HEADERS)
	@mkdir -p $(BUILD_HOST)
	$(HOST_CC) $(HOST_CFLAGS) $(BFS_FUSE_FLAGS) -o $@ \
		tools/bfs_host.c tools/bfs_host_common.c $(BFS_FUSE_SRC) \
		$(HOST_POSIX_OBJ) $(HOST_LIB) $(FUSE_LIBS)

$(BUILD_HOST)/test_%: tests/test_%.c $(CORE_SRC) $(EMU_SRC) $(HOST_HEADERS)
	@mkdir -p $(BUILD_HOST)
	$(HOST_CC) $(HOST_CFLAGS) -o $@ $< $(CORE_SRC) $(EMU_SRC)

$(BUILD_HOST)/test_posix_bio: tests/test_posix_bio.c $(HOST_LIB) $(HOST_POSIX_OBJ) $(HOST_HEADERS)
	@mkdir -p $(BUILD_HOST)
	$(HOST_CC) $(HOST_CFLAGS) -o $@ $< $(HOST_POSIX_OBJ) $(HOST_LIB)

$(BUILD_HOST)/test_posix_faults: tests/test_posix_faults.c tests/posix_bio_faults.c \
		src/host/posix_bio.c $(HOST_LIB) $(HOST_HEADERS)
	@mkdir -p $(BUILD_HOST)
	$(HOST_CC) $(HOST_CFLAGS) -DBFS_POSIX_BIO_FAULT_TEST -o $@ \
		tests/test_posix_faults.c tests/posix_bio_faults.c src/host/posix_bio.c $(HOST_LIB)

$(BUILD_HOST)/test_fsck: $(BUILD_HOST)/bfs

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
AMIGA_TOOL_FLAGS = -std=c99 $(AMIGA_WARNINGS) -Os -m68020 -noixemul -I$(AMIGA_PREFIX)/ndk-include \
                   -DBFS_VERSION=\"$(BFS_VERSION)\"
AMIGA_TOOL_LDFLAGS = -B$(AMIGA_PREFIX)/libnix/lib/ \
                     -L$(AMIGA_PREFIX)/libnix/lib -L$(AMIGA_PREFIX)/lib -lamiga -s
TOOL_SRCS_TEST = tools/bfs-test.c
TOOL_SRCS_BFS = tools/bfs.c tools/bfs_common.c tools/bfs_format.c tools/bfs_snapshot.c tools/bfs_check.c

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
		-o build/release/bfs $(TOOL_SRCS_BFS) $(AMIGA_TOOL_LDFLAGS) -Wl,-Map,build/link-maps/bfs.map
	@cp build/release/bfshandler.020 build/release/bfshandler
	@python3 tools/release/build_identity.py finish
	@echo "Done. Binaries in build/release/"
	@ls -la build/release/

# ── Amiga test binary ───────────────────────────────────────
amiga-test: amiga
	@mkdir -p $(BUILD_AMIGA)
	$(AMIGA_CC) $(AMIGA_TOOL_FLAGS) \
		-o $(BUILD_AMIGA)/bfs-test tools/bfs-test.c $(AMIGA_TOOL_LDFLAGS)

.PHONY: compatibility-test
compatibility-test: amiga $(BUILD_HOST)/bfs
	$(AMIGA_CC) $(AMIGA_TOOL_FLAGS) \
		-o build/amiga/compatibility-probe tests/amiga/compatibility_probe.c $(AMIGA_TOOL_LDFLAGS)
	$(AMIGA_CC) $(AMIGA_TOOL_FLAGS) \
		-o build/amiga/cli-fixture tests/amiga/cli_fixture.c $(AMIGA_TOOL_LDFLAGS)
	$(AMIGA_CC) $(AMIGA_TOOL_FLAGS) \
		-o build/amiga/bfs $(TOOL_SRCS_BFS) $(AMIGA_TOOL_LDFLAGS)
	python3 emulator-test/compatibility-test.py

# ── CI integration test ─────────────────────────────────────
ci-test: amiga amiga-test $(BUILD_HOST)/bfs
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
