# C runtime linked into every Ofir program → $(BUILD)/libofir_rt.a

include ../tools/platform.mk

CC    ?= cc
AR    ?= ar
BUILD ?= ../build

CFLAGS := -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Wpedantic -Werror -O2 -Iinclude
ifneq ($(OS),Windows_NT)
CFLAGS += -fPIC   # a shared library links the runtime in (ADR 0064)
endif

SRCS := $(shell find src -name '*.c' | sort)
OBJS := $(patsubst src/%.c,$(BUILD)/runtime/%.o,$(SRCS))

.PHONY: all clean

ifeq ($(SRCS),)
all:
	@echo "runtime: no sources yet (roadmap task 1.17)"
else
all: $(BUILD)/libofir_rt.a
endif

$(BUILD)/libofir_rt.a: $(OBJS)
	$(AR) rcs $@ $^

$(BUILD)/runtime/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -rf $(BUILD)/runtime $(BUILD)/libofir_rt.a
