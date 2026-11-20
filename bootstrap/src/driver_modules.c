// Loading the modules of a program (spec 7.1 and 7.2): the root file, then
// every module it imports, transitively. `import a.b` is the file
// `<root dir>/a/b.of` or `<root dir>/a/b/index.of`, where the root dir is
// the project's (the nearest directory with an `ofir.json`) or the root
// file's; `import std.x` is in the standard library, and `std.core` (the
// prelude) is always loaded. When
// `a.b` names a declaration rather than a module (`import
// std.collections.Map`), its module `a` is loaded. Imports that match no
// file are reported by the resolver (E0204).

#include "driver_internal.h"

#include "base/platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct {
    Pipeline *p;
    Vec(ProgramModule) modules;
    char root_dir[PATH_SIZE];
    char std_dir[PATH_SIZE];
    bool has_std;
} Loader;

static StrView dotted_name(Loader *ld, SymbolList path, uint32_t count) {
    StrBuf buf = {0};
    for (uint32_t i = 0; i < count; i++) {
        strbuf_append_cstr(&buf, i ? "." : "");
        strbuf_append(&buf, symbol_name(&ld->p->session->names, path.items[i]));
    }
    char *text = arena_strndup(&ld->p->session->arena, buf.data ? buf.data : "", buf.len);
    StrView name = {text, buf.len};
    strbuf_free(&buf);
    return name;
}

static bool is_loaded(const Loader *ld, StrView name) {
    for (size_t i = 0; i < ld->modules.len; i++) {
        if (sv_eq(ld->modules.data[i].name, name)) {
            return true;
        }
    }
    return false;
}

// The file of module `name`, if it exists.
static bool module_file(Loader *ld, StrView name, char out[PATH_SIZE]) {
    bool in_std = name.len >= 4 && memcmp(name.ptr, "std.", 4) == 0;
    if (in_std && !ld->has_std) {
        return false;
    }
    const char *dir = in_std ? ld->std_dir : ld->root_dir;
    StrView rest = in_std ? (StrView){name.ptr + 4, name.len - 4} : name;
    char relative[PATH_SIZE];
    size_t len = rest.len < PATH_SIZE - 1 ? rest.len : PATH_SIZE - 1;
    for (size_t i = 0; i < len; i++) {
        relative[i] = rest.ptr[i] == '.' ? '/' : rest.ptr[i];
    }
    relative[len] = '\0';
    static const char *const FORMS[] = {"%s/%s.of", "%s/%s/index.of"};
    for (size_t i = 0; i < ARRAY_LEN(FORMS); i++) {
        int n = snprintf(out, PATH_SIZE, FORMS[i], dir, relative);
        if (n > 0 && n < PATH_SIZE && source_is_file(out)) {
            return true;
        }
    }
    return false;
}

// `ofir test` (spec 12.2): the tests of `a/b.of` are in `a/b_test.of`, part
// of the same module, so that they may use its private declarations. The
// test file of a module outside std, or of the root, joins its items.
static void add_test_file(Loader *ld, Module *module, StrView name, const char *path) {
    bool in_std = name.len >= 3 && memcmp(name.ptr, "std", 3) == 0;
    size_t len = strlen(path);
    char test[PATH_SIZE];
    if (strcmp(ld->p->opts->command, "test") != 0 || (in_std && ld->modules.len > 0) ||
        len < 3 || snprintf(test, sizeof test, "%.*s_test.of", (int)(len - 3), path) >=
                       (int)sizeof test || access(test, R_OK) != 0) {
        return;
    }
    FileId file;
    Next next;
    Module *tests = source_map_load(&ld->p->session->sources, test, &file)
                        ? parse_file(ld->p, file, false, &next)
                        : NULL;
    if (!tests || tests->items.len == 0) {
        return;
    }
    uint32_t count = module->items.len + tests->items.len;
    Item **items = ARENA_ARRAY(&ld->p->session->arena, Item *, count);
    memcpy(items, module->items.items, module->items.len * sizeof(Item *));
    memcpy(items + module->items.len, tests->items.items, tests->items.len * sizeof(Item *));
    module->items = (ItemList){items, count};
}

static void load(Loader *ld, StrView name, const char *path) {
    FileId file;
    if (!source_map_load(&ld->p->session->sources, path, &file)) {
        return;   // GCOVR_EXCL_LINE: module_file found a regular file
    }
    Next next;
    Module *module = parse_file(ld->p, file, false, &next);
    if (module) {
        add_test_file(ld, module, name, path);
        VEC_PUSH(ld->modules, ((ProgramModule){.module = module, .name = name}));
    }
}

static void load_import(Loader *ld, const Item *item) {
    SymbolList path = item->as.import.path;
    for (uint32_t count = path.len; count >= 1 && count + 1 >= path.len; count--) {
        StrView name = dotted_name(ld, path, count);   // the module, then its parent
        char file[PATH_SIZE];
        if (is_loaded(ld, name)) {
            return;
        }
        if (module_file(ld, name, file)) {
            load(ld, name, file);
            return;
        }
    }
}

// The module name of the root file: its name without the directory and
// `.of`; the directory's name for an `index.of` (spec 7.1).
static StrView root_name(Loader *ld, const char *input) {
    const char *slash = strrchr(input, '/');
    const char *base = slash ? slash + 1 : input;
    size_t len = strlen(base);
    if (strcmp(base, "index.of") == 0 && slash) {
        const char *dir = slash;
        while (dir > input && dir[-1] != '/') {
            dir--;
        }
        base = dir;
        len = (size_t)(slash - dir);
    } else if (len > 3 && strcmp(base + len - 3, ".of") == 0) {
        len -= 3;
    }
    return (StrView){arena_strndup(&ld->p->session->arena, base, len), len};
}

// The dotted module name of `input` relative to the directory `dir`, with
// `prefix` (`std.` for the standard library), if `input` is inside it.
static bool name_under(Loader *ld, const char *input, const char *dir, const char *prefix,
                       StrView *name) {
    char file[PLATFORM_PATH_SIZE], base[PLATFORM_PATH_SIZE];
    if (!platform_realpath(input, file) || !platform_realpath(dir, base)) {
        return false;
    }
    size_t n = strlen(base);
    if (strncmp(file, base, n) != 0 || file[n] != '/') {
        return false;
    }
    char dotted[PATH_SIZE];
    int len = snprintf(dotted, sizeof dotted, "%s%s", prefix, file + n + 1);
    int min = (int)strlen(prefix) + 4;   // at least `x.of`
    if (len < min || len >= (int)sizeof dotted) {
        return false;
    }
    len -= len > min + 5 && strcmp(dotted + len - 9, "/index.of") == 0 ? 9 : 3;   // or `.of`
    for (int i = 0; i < len; i++) {
        dotted[i] = dotted[i] == '/' ? '.' : dotted[i];
    }
    *name = (StrView){arena_strndup(&ld->p->session->arena, dotted, (size_t)len), (size_t)len};
    return true;
}

// The project root (spec 7.2): the nearest directory above the root file
// that holds an `ofir.json`; imports are resolved from there.
static bool project_dir(const char *input, char out[PATH_SIZE]) {
    char path[PLATFORM_PATH_SIZE];
    if (!platform_realpath(input, path)) {
        return false;   // GCOVR_EXCL_LINE: the input was just read
    }
    for (char *slash = strrchr(path, '/'); slash && slash > path; slash = strrchr(path, '/')) {
        *slash = '\0';
        char manifest[PATH_SIZE];
        if (snprintf(manifest, sizeof manifest, "%s/ofir.json", path) < (int)sizeof manifest &&
            access(manifest, R_OK) == 0) {
            return snprintf(out, PATH_SIZE, "%s", path) < PATH_SIZE;
        }
    }
    return false;
}

// The name of the root module. A file inside the standard library is that
// std module (`ofir test std/core/str.of` tests `std.core.str`) and a file
// of a project is named from the project root (`base.intern`), so that an
// import of it does not load it a second time.
static StrView root_module_name(Loader *ld, const char *input, bool in_project) {
    StrView name;
    if (ld->has_std && name_under(ld, input, ld->std_dir, "std.", &name)) {
        return name;
    }
    if (in_project && name_under(ld, input, ld->root_dir, "", &name)) {
        return name;
    }
    return root_name(ld, input);
}

// `file` (canonical) relative to the canonical directory `dir`, with
// `prefix` before it, into `out`, if it is inside it.
static bool under(const char *file, const char *dir, const char *prefix, char *out) {
    size_t n = strlen(dir);
    if (n == 0 || strncmp(file, dir, n) != 0 || file[n] != '/') {
        return false;
    }
    return snprintf(out, PLATFORM_PATH_SIZE, "%s%s", prefix, file + n + 1) <
           PLATFORM_PATH_SIZE;
}

// The logical paths of the files read (ADR 0083): `std/<path>` in the
// standard library and relative to the root directory in the project; any
// other file keeps its path.
static void set_logical_paths(Loader *ld) {
    char std[PLATFORM_PATH_SIZE] = "", root[PLATFORM_PATH_SIZE] = "";
    if (ld->has_std && !platform_realpath(ld->std_dir, std)) {
        std[0] = '\0';
    }
    if (!platform_realpath(ld->root_dir, root)) {
        root[0] = '\0';   // GCOVR_EXCL_LINE: the root directory holds the input just read
    }
    SourceMap *map = &ld->p->session->sources;
    for (uint32_t i = 0; i < map->count; i++) {
        char path[PLATFORM_PATH_SIZE], file[PLATFORM_PATH_SIZE], logical[PLATFORM_PATH_SIZE];
        StrView given = map->files[i].path;
        if (given.len >= sizeof path) {
            continue;   // GCOVR_EXCL_LINE: fopen refuses a path of PATH_MAX bytes or more
        }
        memcpy(path, given.ptr, given.len);
        path[given.len] = '\0';
        if (platform_realpath(path, file) &&
            (under(file, std, "std/", logical) || under(file, root, "", logical))) {
            source_set_logical(map, (FileId){i}, sv_from_cstr(logical));
        }
    }
}

Program load_program(Pipeline *p, Module *root) {
    Loader ld = {.p = p};
    const char *slash = strrchr(p->opts->input, '/');
    int dir_len = slash ? (int)(slash - p->opts->input) : 1;
    bool in_project = project_dir(p->opts->input, ld.root_dir);
    if (!in_project) {   // a single file: its own directory
        snprintf(ld.root_dir, sizeof ld.root_dir, "%.*s", dir_len, slash ? p->opts->input : ".");
    }
    static const char *const STD[] = {"../../std", "../lib/ofir/std"};
    ld.has_std = find_installed(p->opts->argv0, "OFIR_STD", STD, ARRAY_LEN(STD), ld.std_dir);
    StrView name = root_module_name(&ld, p->opts->input, in_project);
    add_test_file(&ld, root, name, p->opts->input);
    VEC_PUSH(ld.modules, ((ProgramModule){.module = root, .name = name}));
    char core[PATH_SIZE];
    // The prelude, in every program.
    if (!is_loaded(&ld, SV("std.core")) && module_file(&ld, SV("std.core"), core)) {
        load(&ld, SV("std.core"), core);
    }
    for (size_t m = 0; m < ld.modules.len; m++) {   // grows while it is walked
        ItemList items = ld.modules.data[m].module->items;
        for (uint32_t i = 0; i < items.len; i++) {
            if (items.items[i]->kind == ITEM_IMPORT) {
                load_import(&ld, items.items[i]);
            }
        }
    }
    set_logical_paths(&ld);
    Program program = {.len = (uint32_t)ld.modules.len};
    program.items = ARENA_ARRAY(&p->session->arena, ProgramModule, ld.modules.len);
    memcpy(program.items, ld.modules.data, ld.modules.len * sizeof(ProgramModule));
    VEC_FREE(ld.modules);
    return program;
}
