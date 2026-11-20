// libFuzzer target for the stage 0 front end (roadmap 6.33i).
//
// Each input is one source file, compiled in memory up to the phase that
// OFIR_FUZZ_STOP names: STOP_AFTER_LEX, STOP_AFTER_PARSE or
// STOP_AFTER_CHECK. Its diagnostics are rendered to /dev/null, so the
// renderer runs too. The compiler must return: a crash, a sanitizer
// report, a leak or an exit is a finding (tools/libfuzz.py).
//
//     make -C bootstrap fuzz    # build/fuzz/{lex,parse,check}

#include "driver_internal.h"

#include "syntax/lexer.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef OFIR_FUZZ_STOP
#define OFIR_FUZZ_STOP STOP_AFTER_CHECK
#endif

static char input[PATH_SIZE];   // a path in an empty directory: imports find nothing
static FILE *sink;

int LLVMFuzzerInitialize(int *argc, char ***argv);
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerInitialize(int *argc, char ***argv) {
    (void)argc;
    (void)argv;
    const char *tmp = getenv("TMPDIR");
    char dir[PATH_SIZE];
    snprintf(dir, sizeof dir, "%s/ofir-fuzz-XXXXXX", tmp && tmp[0] ? tmp : "/tmp");
    if (!mkdtemp(dir)) {
        perror("ofir fuzz: mkdtemp");
        abort();
    }
    snprintf(input, sizeof input, "%s/fuzz.of", dir);
    sink = fopen("/dev/null", "w");
    if (!sink) {
        perror("ofir fuzz: /dev/null");
        abort();
    }
    return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    Options opts = {.command = "build", .input = input, .stop_after = OFIR_FUZZ_STOP};
    Timer timer = {0};
    Session session;
    session_init(&session);
    Pipeline p = {.session = &session, .opts = &opts, .timer = &timer};
    p.file = source_map_add(&session.sources, sv_from_cstr(input),
                            (StrView){(const char *)data, size});
    Next next;
    Module *root = parse_file(&p, p.file, true, &next);
    if (next == CONTINUE) {
        Program program = load_program(&p, root);
        if (!diags_has_errors(&session.diags)) {
            Sema sema;
            sema_init(&sema, &session, program);
            resolve_program(&sema);
            if (!diags_has_errors(&session.diags)) {
                check_program(&sema);
            }
            sema_free(&sema);
        }
    }
    diags_render(&session.diags, &session.sources, DIAG_FORMAT_HUMAN, sink);
    session_free(&session);
    return 0;
}
