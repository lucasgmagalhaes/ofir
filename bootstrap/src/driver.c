// The front end: loading, lexing, parsing and checking. The back end is in
// driver_backend.c.

#define _XOPEN_SOURCE 700   // access

#include "driver_internal.h"

#include "base/clock.h"
#include "syntax/lexer.h"
#include "syntax/parser.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

void timer_start(Timer *timer) {
    timer->start = clock_now_ms();
}

void timer_report(Timer *timer, const char *phase, const char *detail) {
    if (timer->enabled) {
        fprintf(stderr, "time: %-8s %9.3f ms  %s\n", phase, clock_now_ms() - timer->start, detail);
    }
}

static Next run_lexer(Pipeline *p, FileId file, TokenList *tokens, bool root) {
    Session *session = p->session;
    timer_start(p->timer);
    lex(session, file, tokens);
    char detail[96];
    const SourceFile *source = source_file(&session->sources, file);
    snprintf(detail, sizeof detail, "%zu bytes, %u lines, %zu tokens", source->text.len,
             source->line_count, tokens->len);
    timer_report(p->timer, "lex", detail);
    if (root && p->opts->emit == EMIT_TOKENS) {
        print_tokens(tokens, &session->sources, &session->names, stdout);
    }
    bool done = p->opts->emit == EMIT_TOKENS || p->opts->stop_after == STOP_AFTER_LEX;
    return diags_has_errors(&session->diags) || done ? STOP : CONTINUE;
}

// Lexes the whole file, for `--emit=tokens` and `--stop-after=lex`, and
// after an error (the file is only checked for lexer errors then).
static Module *lex_only(Pipeline *p, FileId file, bool root, Next *next) {
    TokenList tokens = {0};
    *next = run_lexer(p, file, &tokens, root);   // always STOP
    VEC_FREE(tokens);
    return NULL;
}

// Lexes and parses together, so only the tokens of the current top-level
// item are held (the lex phase is part of the parse time).
Module *parse_file(Pipeline *p, FileId file, bool root, Next *next) {
    Session *session = p->session;
    bool lexing = p->opts->emit == EMIT_TOKENS || p->opts->stop_after == STOP_AFTER_LEX;
    if (lexing || diags_has_errors(&session->diags)) {
        return lex_only(p, file, root, next);
    }
    timer_start(p->timer);
    Module *module = parse(session, file);
    char detail[96];
    const SourceFile *source = source_file(&session->sources, file);
    snprintf(detail, sizeof detail, "%zu bytes, %u lines, %u items (lexed with the parse)",
             source->text.len, source->line_count, module ? module->items.len : 0);
    timer_report(p->timer, "parse", detail);
    if (module && root && p->opts->emit == EMIT_AST) {
        print_module(module, &session->names, stdout);
    }
    bool done = p->opts->emit == EMIT_AST || p->opts->stop_after == STOP_AFTER_PARSE;
    *next = diags_has_errors(&session->diags) || done ? STOP : CONTINUE;
    return module;
}

static Next run_sema(Pipeline *p, Program program) {
    timer_start(p->timer);
    Sema sema;
    sema_init(&sema, p->session, program);
    sema.testing = strcmp(p->opts->command, "test") == 0;
    sema.trace = p->opts->emit == EMIT_NAMES ? stdout : NULL;
    resolve_program(&sema);
    if (sema.trace) {
        sema_free(&sema);
        return STOP;
    }
    bool resolved = !diags_has_errors(&p->session->diags);
    if (resolved) {
        check_program(&sema);
    }
    if (p->opts->emit == EMIT_TYPES) {
        if (resolved) {
            trace_types(&sema, stdout);
        }
        sema_free(&sema);
        return STOP;
    }
    char detail[64];
    snprintf(detail, sizeof detail, "%u modules, %u types", program.len, sema.types.count);
    timer_report(p->timer, "check", detail);
    Next next = STOP;
    if (!diags_has_errors(&p->session->diags) && p->opts->stop_after != STOP_AFTER_CHECK) {
        next = run_backend(p, &sema);
        keep_test_names(p, &sema);
    }
    sema_free(&sema);
    return next;
}

// Runs the phases in order. Returns whether every requested phase finished.
static bool run_phases(Pipeline *p) {
    timer_start(p->timer);
    if (!source_map_load(&p->session->sources, p->opts->input, &p->file)) {
        fprintf(stderr, "error[E0900]: cannot read `%s`\n", p->opts->input);
        return false;
    }
    timer_report(p->timer, "load", "");
    Next next;
    Module *root = parse_file(p, p->file, true, &next);
    if (next == CONTINUE) {
        Program program = load_program(p, root);
        next = diags_has_errors(&p->session->diags) ? STOP : run_sema(p, program);
    }
    return next != STOP_FAIL && !diags_has_errors(&p->session->diags);
}

// `ofir test a/b_test.of` tests the module `a/b.of`, which loads its test file.
static const char *test_root(const Options *opts, char buf[PATH_SIZE]) {
    size_t len = strlen(opts->input);
    const char *suffix = "_test.of";
    if (strcmp(opts->command, "test") != 0 || len <= strlen(suffix) ||
        strcmp(opts->input + len - strlen(suffix), suffix) != 0 ||
        snprintf(buf, PATH_SIZE, "%.*s.of", (int)(len - strlen(suffix)), opts->input) >=
            PATH_SIZE ||
        access(buf, R_OK) != 0) {
        return opts->input;
    }
    return buf;
}

int driver_build(const Options *given) {
    char root[PATH_SIZE];
    Options copy = *given;
    copy.input = test_root(given, root);
    const Options *opts = &copy;
    Timer total = {.enabled = opts->time_passes};
    Timer phase = total;
    timer_start(&total);
    Session session;
    session_init(&session);
    session.diags.deny_warnings = opts->deny_warnings;
    Pipeline p = {.session = &session, .opts = opts, .timer = &phase};
    bool ok = run_phases(&p);
    diags_render(&session.diags, &session.sources, opts->format, stderr);
    session_free(&session);
    timer_report(&total, "total", "");
    int status = ok ? 0 : 1;
    if (ok && p.exe[0] && strcmp(opts->command, "run") == 0) {
        status = run_program(p.exe, NULL);
    } else if (ok && p.exe[0] && strcmp(opts->command, "test") == 0) {
        status = run_tests(&p);
    }
    free_test_names(&p);
    return status;
}
