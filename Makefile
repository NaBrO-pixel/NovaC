# NovaC build (D-002).
#
#   make                 build the compiler: build/release-<cc>/ncc
#   make test            build with sanitizers and run every test suite
#   make test-all        `make test` with both CC=gcc and CC=clang
#   make check-docs      documentation consistency checks
#   make fuzz            build the libFuzzer harness (Clang only)
#   make fuzz-run FUZZ_SECONDS=1800   run it on the test corpus
#   make clean
#
# Variables: CC, DEBUG=1 (sanitizers), UPDATE=1 (rewrite golden files),
# PYTHON.

PYTHON ?= python3
CCNAME := $(notdir $(firstword $(CC)))

# LLVM (D-003): located with llvm-config; override with LLVM_CONFIG=...
LLVM_CONFIG ?= $(shell command -v llvm-config-18 2>/dev/null || command -v llvm-config 2>/dev/null)
ifeq ($(LLVM_CONFIG),)
$(error llvm-config not found: install LLVM 18 development files (e.g. llvm-18-dev) or set LLVM_CONFIG)
endif
LLVM_INCLUDE := $(shell $(LLVM_CONFIG) --includedir)
LLVM_LIBDIR := $(shell $(LLVM_CONFIG) --libdir)
LLVM_LIBS := -L$(LLVM_LIBDIR) -Wl,-rpath,$(LLVM_LIBDIR) $(shell $(LLVM_CONFIG) --libs) $(shell $(LLVM_CONFIG) --system-libs)

WARN := -Wall -Wextra -Werror -Wshadow -Wstrict-prototypes -Wmissing-prototypes \
        -Wpointer-arith -Wformat=2 -Wno-format-nonliteral
BASE_CFLAGS := -std=c11 $(WARN) -Isrc -isystem $(LLVM_INCLUDE) -MMD -MP

ifeq ($(DEBUG),1)
CONFIG := debug
OPT_CFLAGS := -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=all
OPT_LDFLAGS := -fsanitize=address,undefined
else
CONFIG := release
OPT_CFLAGS := -O2 -g
OPT_LDFLAGS :=
endif

BUILD ?= build/$(CONFIG)-$(CCNAME)
ALL_CFLAGS = $(BASE_CFLAGS) $(OPT_CFLAGS) $(CFLAGS)
ALL_LDFLAGS = $(OPT_LDFLAGS) $(LDFLAGS)

# Every compiler source except the driver's main() forms a library that the
# driver, the unit tests and the fuzzer link.
LIB_SRC := $(filter-out src/driver/main.c,$(wildcard src/*/*.c))
LIB_OBJ := $(LIB_SRC:%.c=$(BUILD)/%.o)
UNIT_SRC := $(wildcard tests/unit/*.c)
UNIT_OBJ := $(UNIT_SRC:%.c=$(BUILD)/%.o)

.PHONY: all test test-here test-all check check-docs unit fuzz fuzz-run clean help

all: $(BUILD)/ncc

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(ALL_CFLAGS) -c $< -o $@

$(BUILD)/libncc.a: $(LIB_OBJ)
	@rm -f $@
	ar rcs $@ $^

$(BUILD)/ncc: $(BUILD)/src/driver/main.o $(BUILD)/libncc.a
	$(CC) $(ALL_LDFLAGS) -o $@ $^ $(LLVM_LIBS) -lm

$(BUILD)/unit_tests: $(UNIT_OBJ) $(BUILD)/libncc.a
	$(CC) $(ALL_LDFLAGS) -o $@ $^ $(LLVM_LIBS) -lm

unit: $(BUILD)/unit_tests
	$(BUILD)/unit_tests

# `make test` always uses the sanitizer configuration
# (NOVAC_COMPILER_ARCHITECTURE.md §2).
test:
	$(MAKE) DEBUG=1 test-here

test-here: $(BUILD)/ncc $(BUILD)/unit_tests
	$(BUILD)/unit_tests
	$(PYTHON) tests/run_tests.py --ncc $(BUILD)/ncc $(if $(filter 1,$(UPDATE)),--update)
	$(PYTHON) tests/run_tests.py --ncc $(BUILD)/ncc --selftest
	$(PYTHON) tests/abi/gen.py --check
	$(PYTHON) tools/check_docs.py

test-all:
	$(MAKE) CC=gcc test
	$(MAKE) CC=clang test

check: test

check-docs:
	$(PYTHON) tools/check_docs.py

# ---- fuzzing (NOVAC_ROADMAP.md step 2.8) ----
FUZZ_BUILD := build/fuzz
FUZZ_CFLAGS := -std=c11 $(WARN) -Isrc -isystem $(LLVM_INCLUDE) -MMD -MP -O1 -g -fsanitize=fuzzer-no-link,address,undefined -fno-sanitize-recover=all
FUZZ_LIB_OBJ := $(LIB_SRC:%.c=$(FUZZ_BUILD)/%.o)
FUZZ_SECONDS ?= 60

$(FUZZ_BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	clang $(FUZZ_CFLAGS) -c $< -o $@

fuzz: $(FUZZ_BUILD)/fuzz_frontend

$(FUZZ_BUILD)/fuzz_frontend: $(FUZZ_BUILD)/tests/fuzz/fuzz_frontend.o $(FUZZ_LIB_OBJ)
	clang -fsanitize=fuzzer,address,undefined -o $@ $^ $(LLVM_LIBS) -lm

fuzz-run: fuzz
	@mkdir -p $(FUZZ_BUILD)/corpus
	cp tests/*/*.nc $(FUZZ_BUILD)/corpus/ 2>/dev/null || true
	$(FUZZ_BUILD)/fuzz_frontend -max_total_time=$(FUZZ_SECONDS) -max_len=4096 \
	    -dict=tests/fuzz/novac.dict -artifact_prefix=$(FUZZ_BUILD)/ $(FUZZ_BUILD)/corpus

-include $(wildcard $(FUZZ_BUILD)/*/*.d $(FUZZ_BUILD)/*/*/*.d)

clean:
	rm -rf build

help:
	@sed -n '3,13p' Makefile

-include $(LIB_OBJ:.o=.d) $(UNIT_OBJ:.o=.d) $(BUILD)/src/driver/main.d
