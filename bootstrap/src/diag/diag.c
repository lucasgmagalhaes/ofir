#include "diag/diag.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void diags_init(Diagnostics *diags, Arena *arena) {
    diags->arena = arena;
    diags->items.data = NULL;
    diags->items.len = diags->items.cap = 0;
    diags->error_count = 0;
    diags->deny_warnings = false;
}

void diags_free(Diagnostics *diags) {
    VEC_FREE(diags->items);
}

// The formatted text, in the diagnostics arena.
static char *format_text(Diagnostics *diags, const char *format, va_list args) {
    va_list copy;
    va_copy(copy, args);
    int n = vsnprintf(NULL, 0, format, copy);
    va_end(copy);
    char *text = arena_alloc(diags->arena, (size_t)n + 1, 1);
    vsnprintf(text, (size_t)n + 1, format, args);
    return text;
}

// Formats the message of `d` and records it.
static void add(Diagnostics *diags, Diagnostic d, const char *format, va_list args) {
    d.message = format_text(diags, format, args);
    d.seq = (uint32_t)diags->items.len;
    VEC_PUSH(diags->items, d);
    if (d.severity == SEVERITY_ERROR) {
        diags->error_count++;
    }
}

static Diagnostic make(Severity severity, const char *code, Span span) {
    Diagnostic d = {.severity = severity, .span = span};
    snprintf(d.code, sizeof d.code, "%s", code);
    return d;
}

void diag_error(Diagnostics *diags, const char *code, Span span, const char *format, ...) {
    va_list args;
    va_start(args, format);
    add(diags, make(SEVERITY_ERROR, code, span), format, args);
    va_end(args);
}

void diag_warning(Diagnostics *diags, const char *code, Span span, const char *format, ...) {
    va_list args;
    va_start(args, format);
    Severity severity = diags->deny_warnings ? SEVERITY_ERROR : SEVERITY_WARNING;
    add(diags, make(severity, code, span), format, args);
    va_end(args);
}

static void add_note(Diagnostics *diags, NoteKind kind, const char *format, va_list args) {
    if (diags->items.len == 0) {
        return;
    }
    Diagnostic *d = &diags->items.data[diags->items.len - 1];
    if (d->note_count == DIAG_MAX_NOTES) {
        return;
    }
    d->notes[d->note_count++] = (DiagNote){kind, format_text(diags, format, args)};
}

void diag_note(Diagnostics *diags, const char *format, ...) {
    va_list args;
    va_start(args, format);
    add_note(diags, NOTE_NOTE, format, args);
    va_end(args);
}

void diag_help(Diagnostics *diags, const char *format, ...) {
    va_list args;
    va_start(args, format);
    add_note(diags, NOTE_HELP, format, args);
    va_end(args);
}

static int compare(const void *pa, const void *pb) {
    const Diagnostic *a = pa, *b = pb;
    if (a->span.file.id != b->span.file.id) {
        return a->span.file.id < b->span.file.id ? -1 : 1;
    }
    if (a->span.start != b->span.start) {
        return a->span.start < b->span.start ? -1 : 1;
    }
    int code = strcmp(a->code, b->code);
    if (code != 0) {
        return code;
    }
    return a->seq < b->seq ? -1 : a->seq > b->seq;   // qsort is not stable (macOS)
}

static bool same(const Diagnostic *a, const Diagnostic *b) {
    return a->span.file.id == b->span.file.id && a->span.start == b->span.start &&
           strcmp(a->code, b->code) == 0 && strcmp(a->message, b->message) == 0;
}

static const char *severity_name(Severity s) {
    return s == SEVERITY_ERROR ? "error" : "warning";
}

static void render_human(const Diagnostic *d, const SourceMap *map, FILE *out) {
    const SourceFile *file = source_file(map, d->span.file);
    LineCol lc = source_line_col(map, d->span.file, d->span.start);
    StrView line = source_line_text(map, d->span.file, lc.line);

    char number[16];
    int width = snprintf(number, sizeof number, "%u", lc.line);
    fprintf(out, "%s[%s]: %s\n", severity_name(d->severity), d->code, d->message);
    fprintf(out, "%*s--> " SV_FMT ":%u:%u\n", width, "", SV_ARG(file->path), lc.line, lc.column);
    fprintf(out, "%*s |\n", width, "");
    fprintf(out, "%s | " SV_FMT "\n", number, SV_ARG(line));

    // The caret line: tabs are kept so the marker stays under the text.
    uint32_t len = d->span.end > d->span.start ? d->span.end - d->span.start : 1;
    if (lc.column - 1 + len > line.len + 1) {
        len = (uint32_t)line.len + 1 - (lc.column - 1);   // clip spans running past the line
    }
    // Built whole and written once: stderr is unbuffered, and a character
    // at a time is a system call each.
    size_t indent = lc.column - 1;
    char *caret = xmalloc(indent + len + 1);
    for (size_t i = 0; i < indent; i++) {
        caret[i] = line.ptr[i] == '\t' ? '\t' : ' ';
    }
    memset(caret + indent, '^', len);
    caret[indent + len] = '\n';
    fprintf(out, "%*s | ", width, "");
    fwrite(caret, 1, indent + len + 1, out);
    free(caret);
    if (d->note_count > 0) {
        fprintf(out, "%*s |\n", width, "");
    }
    for (uint32_t i = 0; i < d->note_count; i++) {
        const DiagNote *note = &d->notes[i];
        fprintf(out, "%*s = %s: %s\n", width, "", note->kind == NOTE_HELP ? "help" : "note",
                note->message);
    }
    fputc('\n', out);
}

void diags_render(Diagnostics *diags, const SourceMap *map, DiagFormat format, FILE *out) {
    if (diags->items.len > 1) {
        qsort(diags->items.data, diags->items.len, sizeof(Diagnostic), compare);
    }
    for (size_t i = 0; i < diags->items.len; i++) {
        const Diagnostic *d = &diags->items.data[i];
        if (i > 0 && same(d, &diags->items.data[i - 1])) {
            continue;
        }
        if (format == DIAG_FORMAT_SHORT) {
            const SourceFile *file = source_file(map, d->span.file);
            LineCol lc = source_line_col(map, d->span.file, d->span.start);
            fprintf(out, SV_FMT ":%u:%u: %s[%s]: %s\n", SV_ARG(file->path), lc.line, lc.column,
                    severity_name(d->severity), d->code, d->message);
        } else {
            render_human(d, map, out);
        }
    }
}
