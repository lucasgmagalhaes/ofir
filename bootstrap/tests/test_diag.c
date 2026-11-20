#include "diag/diag.h"
#include "test.h"

#include <stdio.h>
#include <string.h>

static char *render(Diagnostics *diags, SourceMap *map, DiagFormat format) {
    static char buffer[4096];
    FILE *f = tmpfile();   // not fmemopen: MinGW has none
    diags_render(diags, map, format, f);
    rewind(f);
    size_t n = fread(buffer, 1, sizeof buffer - 1, f);
    buffer[n] = '\0';
    fclose(f);
    return buffer;
}

static void test_render(void) {
    Arena arena;
    arena_init(&arena);
    SourceMap map;
    source_map_init(&map, &arena);
    Diagnostics diags;
    diags_init(&diags, &arena);

    FileId id = source_map_add(&map, SV("main.of"), SV("fn main() {\n    let x = $\n}\n"));
    diag_warning(&diags, "W0001", (Span){id, 20, 21}, "unused variable `%s`", "x");
    diag_error(&diags, "E0001", (Span){id, 24, 25}, "unexpected character");
    diag_error(&diags, "E0001", (Span){id, 24, 25}, "unexpected character");   // duplicate
    CHECK(diags_has_errors(&diags));
    CHECK(diags.error_count == 2);

    char *short_form = render(&diags, &map, DIAG_FORMAT_SHORT);
    CHECK(strcmp(short_form,
                 "main.of:2:9: warning[W0001]: unused variable `x`\n"
                 "main.of:2:13: error[E0001]: unexpected character\n") == 0);

    char *human = render(&diags, &map, DIAG_FORMAT_HUMAN);
    CHECK(strstr(human,
                 "error[E0001]: unexpected character\n"
                 " --> main.of:2:13\n"
                 "  |\n"
                 "2 |     let x = $\n"
                 "  |             ^\n") != NULL);

    diags_free(&diags);
    source_map_free(&map);
    arena_free(&arena);
}

static void test_note_limits(void) {
    Arena arena;
    arena_init(&arena);
    Diagnostics diags;
    diags_init(&diags, &arena);
    diag_help(&diags, "a note before any diagnostic is dropped");
    CHECK(diags.items.len == 0);
    diag_error(&diags, "E0001", (Span){{0}, 0, 1}, "unexpected character");
    for (int i = 0; i < DIAG_MAX_NOTES + 2; i++) {
        diag_help(&diags, "help %d", i);
    }
    CHECK(diags.items.data[0].note_count == DIAG_MAX_NOTES);
    diags_free(&diags);
    arena_free(&arena);
}

int main(void) {
    test_render();
    test_note_limits();
    return test_report("diag");
}
