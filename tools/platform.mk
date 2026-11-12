# Host toolchain defaults, included by the Makefiles.
#
# On Windows the compiler, the runtime and the programs are built with clang
# for the MinGW target (x86_64-w64-windows-gnu), since the runtime uses the
# MinGW C library; the sanitizers are not available there. clang is taken
# from PATH, else from the default LLVM install directory; OFIR_CLANG, the
# clang the compilers run, is set to it.

ifeq ($(OS),Windows_NT)
EXE := .exe
CLANG_TARGET := --target=x86_64-w64-windows-gnu
# MinGW keeps the pthread functions in winpthreads.
LINK_LIBS := -lm -lpthread
ifeq ($(origin OFIR_CLANG),undefined)
ifneq ($(shell command -v clang 2>/dev/null),)
OFIR_CLANG := clang
else
OFIR_CLANG := C:/Program Files/LLVM/bin/clang.exe
# Programs that run `clang` by name (tests/run/system/os.of) find it too.
export PATH := C:\Program Files\LLVM\bin;$(PATH)
endif
endif
export OFIR_CLANG
ifeq ($(origin CC),default)
CC := "$(OFIR_CLANG)" $(CLANG_TARGET)
endif
SANITIZE ?=
PYTHON ?= python
# The tools read and write UTF-8, not the console code page.
export PYTHONUTF8 := 1
else
EXE :=
CLANG_TARGET :=
LINK_LIBS := -lm
endif
