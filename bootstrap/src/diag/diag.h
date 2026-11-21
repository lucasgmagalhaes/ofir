// Diagnostics: collected during compilation, rendered at the end.
//
// Codes come from the registry in docs/errors/README.md.

#ifndef OFIR_DIAG_H
#define OFIR_DIAG_H

#include "base/source.h"
#include "base/vec.h"

#include <stdarg.h>
#include <stdio.h>

typedef enum {
    SEVERITY_ERROR,
    SEVERITY_WARNING,
} Severity;

typedef enum {
    NOTE_NOTE,           // context: `= note: ...`
    NOTE_HELP,           // a fix: `= help: ...`
} NoteKind;

#define DIAG_MAX_NOTES 4

typedef struct {
    NoteKind kind;
    char *message;       // owned by the diagnostics arena
} DiagNote;

typedef struct {
    Severity severity;
    char code[8];        // "E0001", "W0002"
    Span span;
    char *message;       // owned by the diagnostics arena
    uint32_t seq;        // the order it was reported in: equal positions keep it
    uint32_t note_count;
    DiagNote notes[DIAG_MAX_NOTES];
} Diagnostic;

typedef enum {
    DIAG_FORMAT_HUMAN,   // source excerpt with a caret
    DIAG_FORMAT_SHORT,   // file:line:col: error[E0001]: message
} DiagFormat;

typedef struct {
    Arena *arena;
    Vec(Diagnostic) items;
    uint32_t error_count;
    bool deny_warnings;  // --deny-warnings: warnings are reported as errors
} Diagnostics;

void diags_init(Diagnostics *diags, Arena *arena);
void diags_free(Diagnostics *diags);

void diag_error(Diagnostics *diags, const char *code, Span span, const char *format, ...)
    __attribute__((format(printf, 4, 5)));
// `diag_error` with its arguments in a `va_list`.
void diag_errorv(Diagnostics *diags, const char *code, Span span, const char *format,
                 va_list args) __attribute__((format(printf, 4, 0)));
void diag_warning(Diagnostics *diags, const char *code, Span span, const char *format, ...)
    __attribute__((format(printf, 4, 5)));

// A help line on the diagnostic reported last (spec: toolchain.md).
void diag_help(Diagnostics *diags, const char *format, ...) __attribute__((format(printf, 2, 3)));

static inline bool diags_has_errors(const Diagnostics *diags) {
    return diags->error_count > 0;
}

// Sorts by position, removes duplicates and writes every diagnostic to `out`.
void diags_render(Diagnostics *diags, const SourceMap *map, DiagFormat format, FILE *out);

#endif
