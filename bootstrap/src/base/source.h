// Source files and positions.
//
// Positions are byte offsets in a file (`Span`); line and column numbers
// are computed only when a diagnostic is rendered.

#ifndef OFIR_SOURCE_H
#define OFIR_SOURCE_H

#include "base/arena.h"
#include "base/str.h"

typedef struct {
    uint32_t id;
} FileId;

typedef struct {
    FileId file;
    uint32_t start;   // byte offset of the first byte
    uint32_t end;     // byte offset one past the last byte
} Span;

typedef struct {
    StrView path;
    StrView logical;   // for panic locations and `source_filename` (ADR 0083)
    StrView text;
    uint32_t *line_starts;   // byte offset where each line begins
    uint32_t line_count;
} SourceFile;

typedef struct {
    Arena *arena;
    SourceFile *files;
    uint32_t count;
    uint32_t cap;
} SourceMap;

typedef struct {
    uint32_t line;     // 1-based
    uint32_t column;   // 1-based, in bytes
} LineCol;

void source_map_init(SourceMap *map, Arena *arena);
void source_map_free(SourceMap *map);

// Adds a file whose text is already in memory (copied into the arena).
FileId source_map_add(SourceMap *map, StrView path, StrView text);

// Reads a file from disk. Returns false and leaves *out untouched on error.
bool source_map_load(SourceMap *map, const char *path, FileId *out);

const SourceFile *source_file(const SourceMap *map, FileId id);

// Sets the logical path of a file (copied into the arena).
void source_set_logical(SourceMap *map, FileId id, StrView logical);
LineCol source_line_col(const SourceMap *map, FileId id, uint32_t offset);

// The text of line `line` (1-based), without its line terminator.
StrView source_line_text(const SourceMap *map, FileId id, uint32_t line);

#endif
