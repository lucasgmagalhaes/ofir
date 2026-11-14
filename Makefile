# Top-level build for the Ofir repository.
# See docs/design/toolchain.md, section 7.

BUILD  ?= build

include tools/platform.mk
PYTHON ?= python3

.PHONY: all stage0 stage1 stage2 verify runtime unit runner-selftest test test-std test-stage1 compare-stages check-format check-fmt-corpus check-codes check-sizes check-docs check-leaks check-asan seed check-seed bench bench-programs bench-compare clean

all: stage0 runtime

stage0:
	$(MAKE) -C bootstrap BUILD=$(abspath $(BUILD))/stage0

# TARGET=triple builds the runtime of another target (ADR 0066).
runtime:
ifeq ($(TARGET),)
	$(MAKE) -C runtime BUILD=$(abspath $(BUILD))
else
	$(MAKE) -C runtime BUILD=$(abspath $(BUILD))/$(TARGET) CC="$(if $(OFIR_CLANG),$(OFIR_CLANG),clang) --target=$(TARGET)"
endif

unit:
	$(MAKE) -C bootstrap test BUILD=$(abspath $(BUILD))/stage0

runner-selftest:
	$(PYTHON) tests/runner/selftest/run_selftest.py

check-codes: stage1 runtime
	$(STAGE_ENV) $(PYTHON) tools/check_error_codes.py --compiler $(BUILD)/stage1/ofir$(EXE)

check-sizes:
	$(PYTHON) tools/check_sizes.py

check-docs:
	$(PYTHON) tools/check_docs.py
	$(PYTHON) tools/check_conformance.py
	$(PYTHON) tools/check_features.py

# No inline function or statement bodies (docs/style-guide.md, `fmt.inlineBlocks`).
check-format:
	$(PYTHON) tools/check_format.py

# `ofir fmt` is idempotent and keeps every comment and the AST of each source.
check-fmt-corpus: stage1 runtime
	$(STAGE_ENV) $(PYTHON) tools/check_fmt_corpus.py --compiler $(BUILD)/stage1/ofir$(EXE)

# Every run test and example under valgrind: no leaks, no invalid accesses.
# Stage 1 compiles every program stage 0 does, and the stage 1 only ones.
check-leaks: stage1 runtime
	$(STAGE_ENV) $(PYTHON) tools/check_leaks.py --compiler $(BUILD)/stage1/ofir$(EXE)

# The `@test` functions of std and of the stage 1 compiler (spec 12.2).
test-std: stage0 runtime
	$(PYTHON) tools/run_std_tests.py --compiler $(BUILD)/stage0/ofir$(EXE)

# Every run and panic test and example under ASan and UBSan (needs gcc).
check-asan: stage1 runtime
	$(STAGE_ENV) $(PYTHON) tools/check_asan.py --compiler $(BUILD)/stage1/ofir$(EXE)

# Stage 1 (compiler/, built by stage 0) gives the same output as stage 0.
compare-stages: stage1
	$(PYTHON) tools/compare_stages.py --compiler $(BUILD)/stage0/ofir$(EXE) --stage1 $(BUILD)/stage1/ofir$(EXE) --fuzz 200

# Stage 1, built once by stage 0 and reused until its inputs change.
STAGE1_SOURCES := $(shell find compiler std -name '*.of' ! -name '*_test.of' -type f)
stage1: stage0 runtime
	$(MAKE) $(BUILD)/stage1/ofir$(EXE)

$(BUILD)/stage1/ofir$(EXE): $(STAGE1_SOURCES) $(BUILD)/stage0/ofir$(EXE) \
		$(BUILD)/libofir_rt.a Makefile tools/platform.mk
	mkdir -p $(BUILD)/stage1
	OFIR_STD=$(abspath std) $(BUILD)/stage0/ofir$(EXE) build compiler/main.of \
	    --release -o $(BUILD)/stage1/ofir$(EXE)

# Stage 2: the compiler built by stage 1 (roadmap 4.13).
STAGE_ENV = OFIR_STD=$(abspath std) OFIR_RUNTIME=$(abspath $(BUILD))/libofir_rt.a
stage2: stage1
	mkdir -p $(BUILD)/stage2
	$(STAGE_ENV) $(BUILD)/stage1/ofir$(EXE) build compiler/main.of -o $(BUILD)/stage2/ofir$(EXE)

# Stage 2 builds itself (stage 3): both write the same LLVM IR for compiler/.
verify: stage2
	mkdir -p $(BUILD)/stage3
	$(STAGE_ENV) $(BUILD)/stage2/ofir$(EXE) build compiler/main.of -o $(BUILD)/stage3/ofir$(EXE)
	$(STAGE_ENV) $(BUILD)/stage2/ofir$(EXE) build compiler/main.of --emit=llvm -o $(BUILD)/stage2/self
	$(STAGE_ENV) $(BUILD)/stage3/ofir$(EXE) build compiler/main.of --emit=llvm -o $(BUILD)/stage3/self
	cmp $(BUILD)/stage2/self.ll $(BUILD)/stage3/self.ll
	@echo "verify: stage 2 and stage 3 generate the same LLVM IR for compiler/"

# The seed of a release (docs/bootstrap.md): the LLVM IR that the compiler
# writes for itself, once stage 2 and stage 3 agree on it.
VERSION := $(shell sed -n 's/^const VERSION: str = "\(.*\)"/\1/p' compiler/main.of)
SEED    ?= $(BUILD)/seed/ofir-$(VERSION).ll
seed: verify
	mkdir -p $(BUILD)/seed
	cp $(BUILD)/stage2/self.ll $(BUILD)/seed/ofir-$(VERSION).ll
	cd $(BUILD)/seed && (sha256sum ofir-$(VERSION).ll 2>/dev/null || \
	    shasum -a 256 ofir-$(VERSION).ll) > ofir-$(VERSION).ll.sha256
	@cat $(BUILD)/seed/ofir-$(VERSION).ll.sha256

# The fast path of the bootstrap: clang builds a compiler from a seed
# (SEED=path) whose SHA-256 is recorded (its .sha256, else seed/SEEDS.md), which
# must build the current compiler; that compiler rebuilds itself to a fixed
# point and passes the tests.
SEED_CLANG = "$(if $(OFIR_CLANG),$(OFIR_CLANG),clang)" $(CLANG_TARGET)
check-seed: runtime
	$(PYTHON) tools/check_seed_sum.py $(SEED)
	mkdir -p $(BUILD)/from-seed
	$(SEED_CLANG) -O2 -Wno-override-module $(SEED) $(BUILD)/libofir_rt.a $(LINK_LIBS) \
	    -o $(BUILD)/from-seed/seed$(EXE)
	$(STAGE_ENV) $(BUILD)/from-seed/seed$(EXE) build compiler/main.of --release \
	    -o $(BUILD)/from-seed/ofir$(EXE)
	$(BUILD)/from-seed/ofir$(EXE) version
	$(STAGE_ENV) $(BUILD)/from-seed/ofir$(EXE) build compiler/main.of -o $(BUILD)/from-seed/self$(EXE)
	$(STAGE_ENV) $(BUILD)/from-seed/ofir$(EXE) build compiler/main.of --emit=llvm -o $(BUILD)/from-seed/a
	$(STAGE_ENV) $(BUILD)/from-seed/self$(EXE) build compiler/main.of --emit=llvm -o $(BUILD)/from-seed/b
	cmp $(BUILD)/from-seed/a.ll $(BUILD)/from-seed/b.ll
	$(STAGE_ENV) $(PYTHON) tests/runner/run_tests.py --compiler $(BUILD)/from-seed/ofir$(EXE) --stage 1

# Every test with stage 1 as the compiler (roadmap 4.8, 4.9).
test-stage1: stage1
	OFIR_STD=$(abspath std) OFIR_RUNTIME=$(abspath $(BUILD))/libofir_rt.a \
	    $(PYTHON) tests/runner/run_tests.py --compiler $(BUILD)/stage1/ofir$(EXE) --stage 1
	OFIR_STD=$(abspath std) OFIR_RUNTIME=$(abspath $(BUILD))/libofir_rt.a \
	    $(PYTHON) tools/run_std_tests.py --compiler $(BUILD)/stage1/ofir$(EXE) --stage 1
	OFIR_STD=$(abspath std) OFIR_RUNTIME=$(abspath $(BUILD))/libofir_rt.a \
	    $(PYTHON) tools/check_object_cache.py --compiler $(BUILD)/stage1/ofir$(EXE)
	OFIR_STD=$(abspath std) OFIR_RUNTIME=$(abspath $(BUILD))/libofir_rt.a \
	    $(PYTHON) tools/check_debugger.py --compiler $(BUILD)/stage1/ofir$(EXE)
	OFIR_STD=$(abspath std) OFIR_RUNTIME=$(abspath $(BUILD))/libofir_rt.a \
	    $(PYTHON) tools/check_service.py --compiler $(BUILD)/stage1/ofir$(EXE)

# The run tests compiled for TARGET and run by OFIR_RUNNER (ADR 0066).
test-cross: stage1
	$(MAKE) runtime TARGET=$(TARGET)
	$(STAGE_ENV) $(PYTHON) tests/runner/run_tests.py --compiler $(BUILD)/stage1/ofir$(EXE) \
	    --stage 1 --target $(TARGET) run

test: stage0 runtime unit runner-selftest check-codes check-sizes check-docs check-format check-fmt-corpus test-std compare-stages test-stage1 verify
	$(PYTHON) tests/runner/run_tests.py --compiler $(BUILD)/stage0/ofir$(EXE)

# Performance (docs/design/performance.md)
bench:
	$(MAKE) -C bootstrap release BUILD=$(abspath $(BUILD))/release
	$(PYTHON) bench/run_bench.py --compiler $(BUILD)/release/ofir$(EXE)

BENCH_PROGRAM_RUNS ?= 5
bench-programs: runtime
	$(MAKE) -C bootstrap release BUILD=$(abspath $(BUILD))/release
	$(STAGE_ENV) $(PYTHON) bench/run_programs.py --compiler $(BUILD)/release/ofir \
	    --runs $(BENCH_PROGRAM_RUNS) --json $(BUILD)/generated-code-results.json

BASE ?= origin/main
bench-compare:
	tools/bench_compare.sh $(BASE)

clean:
	rm -rf $(BUILD)
