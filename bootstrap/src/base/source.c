#include "base/source.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void source_map_init(SourceMap *map, Arena *arena) {
    map->arena = arena;
    map->files = NULL;
    map->count = 0;
    map->cap = 0;
}

void source_map_free(SourceMap *map) {
    free(map->files);
    map->files = NULL;
    map->count = map->cap = 0;
}

// Records the start of every line of `text` with memchr, which scans many
// bytes per instruction.
static void index_lines(SourceMap *map, SourceFile *file) {
    const char *text = file->text.ptr, *end = text + file->text.len;
    uint32_t lines = 1;
    for (const char *p = text; (p = memchr(p, '\n', (size_t)(end - p))) != NULL; p++) {
        lines++;
    }
    file->line_starts = ARENA_ARRAY(map->arena, uint32_t, lines);
    file->line_starts[0] = 0;
    uint32_t line = 1;
    for (const char *p = text; (p = memchr(p, '\n', (size_t)(end - p))) != NULL; p++) {
        file->line_starts[line++] = (uint32_t)(p - text) + 1;
    }
    file->line_count = lines;
}

// Adds a file whose text is already in the arena.
static FileId add_owned(SourceMap *map, StrView path, StrView text) {
    if (text.len > UINT32_MAX - 1) {
        fatal("source file too large: " SV_FMT, SV_ARG(path));
    }
    if (map->count == map->cap) {
        map->cap = map->cap ? map->cap * 2 : 16;
        map->files = xrealloc(map->files, sizeof(SourceFile) * map->cap);
    }
    SourceFile *file = &map->files[map->count];
    file->path = (StrView){arena_strndup(map->arena, path.ptr, path.len), path.len};
    file->logical = file->path;
    file->text = text;
    index_lines(map, file);
    return (FileId){map->count++};
}

FileId source_map_add(SourceMap *map, StrView path, StrView text) {
    StrView copy = {arena_strndup(map->arena, text.ptr, text.len), text.len};
    return add_owned(map, path, copy);
}

// Reads the whole file into the arena with a single allocation.
bool source_map_load(SourceMap *map, const char *path, FileId *out) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        return false;
    }
    bool ok = fseek(f, 0, SEEK_END) == 0;
    long size = ok ? ftell(f) : -1;
    ok = size >= 0 && fseek(f, 0, SEEK_SET) == 0;
    char *text = ok ? arena_alloc_uninit(map->arena, (size_t)size + 1, 1) : NULL;
    ok = ok && fread(text, 1, (size_t)size, f) == (size_t)size;
    fclose(f);
    if (ok) {
        text[size] = '\0';
        *out = add_owned(map, sv_from_cstr(path), (StrView){text, (size_t)size});
    }
    return ok;
}

const SourceFile *source_file(const SourceMap *map, FileId id) {
    return &map->files[id.id];
}

void source_set_logical(SourceMap *map, FileId id, StrView logical) {
    map->files[id.id].logical =
        (StrView){arena_strndup(map->arena, logical.ptr, logical.len), logical.len};
}

LineCol source_line_col(const SourceMap *map, FileId id, uint32_t offset) {
    const SourceFile *file = source_file(map, id);
    // Binary search for the last line starting at or before `offset`.
    uint32_t lo = 0, hi = file->line_count;
    while (hi - lo > 1) {
        uint32_t mid = lo + (hi - lo) / 2;
        if (file->line_starts[mid] <= offset) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    return (LineCol){lo + 1, offset - file->line_starts[lo] + 1};
}

StrView source_line_text(const SourceMap *map, FileId id, uint32_t line) {
    const SourceFile *file = source_file(map, id);
    if (line == 0 || line > file->line_count) {
        return (StrView){"", 0};
    }
    uint32_t start = file->line_starts[line - 1];
    uint32_t end = line < file->line_count ? file->line_starts[line] : (uint32_t)file->text.len;
    while (end > start && (file->text.ptr[end - 1] == '\n' || file->text.ptr[end - 1] == '\r')) {
        end--;
    }
    return (StrView){file->text.ptr + start, end - start};
}

Span span_join(Span a, Span b) {
    return (Span){a.file, a.start < b.start ? a.start : b.start, a.end > b.end ? a.end : b.end};
}
