# Stage 0 compiler (C11).
#   make            debug build with AddressSanitizer and UBSan
#   make release    optimized build
#   make test       build and run the C unit tests in tests/
#   make fuzz       the libFuzzer targets of fuzz/ (clang), in $(FUZZ_BUILD)

include ../tools/platform.mk

CC       ?= cc
BUILD    ?= ../build/stage0
MODE     ?= debug
SANITIZE ?= address,undefined   # empty to disable (e.g. clang without compiler-rt)

WARNINGS := -Wall -Wextra -Wpedantic -Werror -Wshadow -Wstrict-prototypes
CFLAGS   := -std=c11 -D_POSIX_C_SOURCE=200809L $(WARNINGS) -Isrc -Itests

ifeq ($(MODE),release)
CFLAGS  += -O2 -DNDEBUG
LDFLAGS :=
else
CFLAGS  += -O0 -g -fno-omit-frame-pointer
LDFLAGS :=
ifneq ($(strip $(SANITIZE)),)
CFLAGS  += -fsanitize=$(strip $(SANITIZE))
LDFLAGS += -fsanitize=$(strip $(SANITIZE))
endif
endif

SRCS      := $(shell find src -name '*.c' | sort)
LIB_SRCS  := $(filter-out src/main.c,$(SRCS))
OBJS      := $(patsubst src/%.c,$(BUILD)/obj/$(MODE)/%.o,$(SRCS))
LIB_OBJS  := $(patsubst src/%.c,$(BUILD)/obj/$(MODE)/%.o,$(LIB_SRCS))
TESTS     := $(patsubst tests/%.c,$(BUILD)/tests/%$(EXE),$(wildcard tests/*.c))

.PHONY: all release test fuzz clean

all: $(BUILD)/ofir$(EXE)

release:
	$(MAKE) MODE=release

$(BUILD)/ofir$(EXE): $(OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(LDFLAGS) -o $@ $^ -lm

$(BUILD)/obj/$(MODE)/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c -o $@ $<

$(BUILD)/tests/%$(EXE): tests/%.c $(LIB_OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ -lm

test: $(TESTS)
	@for t in $(TESTS); do echo "unit: $$t"; $$t || exit 1; done
	@echo "unit: all $(words $(TESTS)) test programs passed"

# libFuzzer (roadmap 6.33i): the front end built with clang and
# -fsanitize=fuzzer-no-link, one target per phase it stops after.
FUZZ_BUILD   ?= ../build/fuzz
FUZZ_CC      ?= clang
FUZZ_FLAGS   := -std=c11 -D_POSIX_C_SOURCE=200809L $(WARNINGS) -Isrc -O1 -g \
                -fno-omit-frame-pointer -fsanitize=fuzzer-no-link,address,undefined \
                -fno-sanitize-recover=undefined
FUZZ_OBJS    := $(patsubst src/%.c,$(FUZZ_BUILD)/obj/%.o,$(LIB_SRCS))
FUZZ_TARGETS := lex:STOP_AFTER_LEX parse:STOP_AFTER_PARSE check:STOP_AFTER_CHECK

fuzz: $(foreach t,$(FUZZ_TARGETS),$(FUZZ_BUILD)/$(firstword $(subst :, ,$(t))))

$(FUZZ_BUILD)/obj/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(FUZZ_CC) $(FUZZ_FLAGS) -MMD -MP -c -o $@ $<

define fuzz_target
$(FUZZ_BUILD)/$(1): fuzz/frontend.c $(FUZZ_OBJS)
	$(FUZZ_CC) $(FUZZ_FLAGS) -fsanitize=fuzzer -DOFIR_FUZZ_STOP=$(2) -o $$@ $$^ -lm
endef
$(foreach t,$(FUZZ_TARGETS),$(eval $(call fuzz_target,$(word 1,$(subst :, ,$(t))),$(word 2,$(subst :, ,$(t))))))

clean:
	rm -rf $(BUILD)

-include $(OBJS:.o=.d)
-include $(FUZZ_OBJS:.o=.d)
