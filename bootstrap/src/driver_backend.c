// The back end: lowering, LLVM IR, clang (docs/design/toolchain.md, 4).

#include "driver_internal.h"

#include "air/air_print.h"
#include "air/accumulate.h"
#include "air/fold.h"
#include "air/lower.h"
#include "base/platform.h"
#include "codegen/llvm.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// The executable path: `-o`, else the input's name without `.of` (for
// `run`, in a temporary directory).
static bool output_path(const Options *opts, char out[PATH_SIZE]) {
    if (opts->output) {
        return snprintf(out, PATH_SIZE, "%s", opts->output) < PATH_SIZE;
    }
    const char *base = strrchr(opts->input, '/');
    base = base ? base + 1 : opts->input;
    size_t len = strlen(base);
    if (len > 3 && strcmp(base + len - 3, ".of") == 0) {
        len -= 3;
    }
    if (strcmp(opts->command, "run") != 0 && strcmp(opts->command, "test") != 0) {
        return snprintf(out, PATH_SIZE, "%.*s", (int)len, base) < PATH_SIZE;
    }
    char dir[PATH_SIZE];
    if (!platform_make_temp_dir("ofir-run-", dir)) {
        return false;
    }
    return snprintf(out, PATH_SIZE, "%s/%.*s", dir, (int)len, base) < PATH_SIZE;
}

int run_program(const char *exe, const char *arg) {
    char *argv[] = {(char *)exe, (char *)arg, NULL};
    int status = platform_spawn_wait(argv);
    return status < 0 ? 1 : status;
}

static Next link_program(const Options *opts, const char *ll, const char *exe) {
    char runtime[PATH_SIZE];
    static const char *const PLACES[] = {"../libofir_rt.a", "../lib/ofir/libofir_rt.a"};
    if (!find_installed(opts->argv0, "OFIR_RUNTIME", PLACES, ARRAY_LEN(PLACES), runtime)) {
        fprintf(stderr, "error[E0902]: cannot find the runtime library libofir_rt.a "
                        "(set OFIR_RUNTIME)\n");
        return STOP_FAIL;
    }
    const char *clang = getenv("OFIR_CLANG");
    // The module has no target triple: clang uses its default target, except
    // on Windows, where the runtime is built for MinGW rather than MSVC and
    // the main thread gets the 8 MiB stack Linux gives it, not 1 MiB.
    char *argv[] = {(char *)(clang && clang[0] ? clang : "clang"),
#ifdef _WIN32
                    "--target=x86_64-w64-windows-gnu", "-Wl,--stack,8388608",
#endif
                    opts->release ? "-O2" : "-O0", "-Wno-override-module", (char *)ll, runtime,
                    "-lm",
#ifdef _WIN32
                    // MinGW keeps the pthread functions in winpthreads.
                    "-lpthread",
#endif
                    "-o", (char *)exe, NULL};
    if (platform_spawn_wait(argv) != 0) {
        fprintf(stderr, "error[E0902]: clang failed to compile the generated code\n");
        return STOP_FAIL;
    }
    return STOP;
}

// Each lowered function is validated, its checks folded (air/fold.h), it is
// validated again, then printed (--emit=air) or turned into LLVM IR.
typedef struct {
    Sema *sema;
    Gen *gen;     // NULL for --emit=air
} Sink;

// Stops on a malformed body: a compiler bug, reported before LLVM.
static void validate(const Sink *sink, const AirBody *body, const char *pass) {
    char error[256];
    if (!air_validate_body(body, error, sizeof error)) {
        air_print_body(body, sink->sema, stderr);   // what the bug report needs
        fputs("note: this is a compiler bug; a report needs the AIR above\n", stderr);
        fatal("invalid AIR after %s: %s", pass, error);
    }
}

static void take_body(void *context, AirBody *body) {
    Sink *sink = context;
    validate(sink, body, "lowering");
    air_fold_checks(body, &sink->sema->types);
    air_accumulate(body, &sink->sema->types);
    validate(sink, body, "check folding");
    if (sink->gen) {
        llvm_function(sink->gen, body);
    } else {
        air_print_body(body, sink->sema, stdout);
    }
}

// Lowers and writes `<exe>.ll`; returns false if lowering reported errors.
static bool write_llvm(Pipeline *p, Sema *sema, const char *ll) {
    FILE *out = fopen(ll, "wb");
    if (!out) {
        fprintf(stderr, "error[E0902]: cannot write `%s`: %s\n", ll, strerror(errno));
        return false;
    }
    timer_start(p->timer);
    Sink sink = {.sema = sema, .gen = llvm_begin(sema, out)};
    lower_program(sema, take_body, &sink);
    llvm_end(sink.gen);
    fclose(out);
    timer_report(p->timer, "llvm", ll);
    return !diags_has_errors(&sema->session->diags);
}

static Next generate(Pipeline *p, Sema *sema) {
    char *exe = p->exe;
    char ll[PATH_SIZE + 3];
    if (!output_path(p->opts, exe) || snprintf(ll, sizeof ll, "%s.ll", exe) >= (int)sizeof ll) {
        fprintf(stderr, "error[E0902]: cannot create the output path\n");
        return STOP_FAIL;
    }
    bool ok = write_llvm(p, sema, ll);
    Next next = ok ? STOP : STOP_FAIL;
    if (ok && p->opts->emit != EMIT_LLVM) {
        timer_start(p->timer);
        next = link_program(p->opts, ll, exe);
        timer_report(p->timer, "clang", exe);
    }
    if (!ok || p->opts->emit != EMIT_LLVM) {
        remove(ll);
    }
    if (next != STOP || p->opts->emit == EMIT_LLVM) {
        exe[0] = '\0';
    }
    return next;
}

Next run_backend(Pipeline *p, Sema *sema) {
    if (p->opts->emit == EMIT_AIR) {
        Sink sink = {.sema = sema};
        lower_program(sema, take_body, &sink);
        return STOP;
    }
    return generate(p, sema);
}
