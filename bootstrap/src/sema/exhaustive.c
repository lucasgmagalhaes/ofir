// Exhaustiveness and reachability of `match` arms: the usefulness algorithm
// of Maranget, "Warnings for pattern matching" (2007), as in
// docs/design/compiler-architecture.md, 6.
//
// A row of patterns is *useful* after a matrix of rows when some value
// matches it and no row of the matrix. A match is exhaustive when `_` is
// not useful after its unguarded arms, and an arm is unreachable when it is
// not useful after the unguarded arms before it. Usefulness also gives a
// witness: a value that no arm matches, shown in the error message.
//
// Types with finitely many constructors are enums (one per variant), bool,
// tuples, structs and (): a column whose constructors are all present is
// split by constructor. Other types (integers, floats, str) have infinitely
// many: literals and ranges never cover them, only `_` or a binding does.

#include "sema/check_internal.h"

#include <string.h>

typedef struct {
    const Pattern **items;   // NULL: a wildcard
    uint32_t len;
} Row;

typedef struct {
    Row *rows;
    uint32_t len, cap;
} Matrix;

typedef struct {
    Checker *c;
    Arena *arena;
} Ctx;

enum { WILD = -1, OPAQUE = -2 };   // heads that are not a finite constructor

// ---------------------------------------------------------------------------
// Types and constructors

// The number of constructors of `type`, 0 when there are infinitely many.
static uint32_t ctor_count(Ctx *x, TypeId type) {
    const Ty *ty = type_get(x->c->types, type);
    if (ty->kind == TY_BOOL) {
        return 2;
    }
    if (ty->kind == TY_TUPLE || ty->kind == TY_UNIT) {
        return 1;
    }
    if (ty->kind != TY_ADT) {
        return 0;
    }
    const Def *def = sema_def(x->c->sema, (DefId){ty->data});
    if (def->kind == DEF_ENUM) {
        return def->variants ? def->variants->count : 0;
    }
    return def->fields ? 1 : 0;
}

static uint32_t arity(Ctx *x, TypeId type, uint32_t ctor) {
    const Ty *ty = type_get(x->c->types, type);
    if (ty->kind == TY_TUPLE) {
        return ty->arg_count;
    }
    if (ty->kind != TY_ADT) {
        return 0;   // bool, ()
    }
    const Def *def = sema_def(x->c->sema, (DefId){ty->data});
    return def->kind == DEF_ENUM ? def->variants->variants[ctor].count : def->fields->count;
}

static TypeId field_type(Ctx *x, TypeId type, uint32_t ctor, uint32_t i) {
    const Ty *ty = type_get(x->c->types, type);
    if (ty->kind == TY_TUPLE) {
        return ty->args[i];
    }
    const Def *def = sema_def(x->c->sema, (DefId){ty->data});
    return def->kind == DEF_ENUM ? variant_field_type(x->c->sema, type, ctor, i)
                                 : struct_field_type(x->c->sema, type, i);
}

// The constructor at the head of `p` (not an or-pattern).
static int64_t head(const Pattern *p) {
    if (!p) {
        return WILD;
    }
    switch (p->kind) {
    case PAT_WILDCARD: case PAT_BINDING: return WILD;
    case PAT_VARIANT: return p->as.variant.index;
    case PAT_TUPLE: case PAT_STRUCT: case PAT_TUPLE_STRUCT: return 0;
    case PAT_LITERAL:
        if (p->as.literal->kind == EXPR_BOOL) {
            return p->as.literal->as.bool_value ? 1 : 0;
        }
        return OPAQUE;
    default: return OPAQUE;   // other literals and ranges
    }
}

// The pattern of field `i` in `Point{x: 0, ..}`; NULL when left out.
static const Pattern *struct_field(Ctx *x, const Pattern *p, uint32_t i) {
    const Def *def = sema_def(x->c->sema, (DefId){type_get(x->c->types, p->type)->data});
    Symbol name = def->fields->names[i];
    for (uint32_t f = 0; f < p->as.struct_.fields.len; f++) {
        if (symbol_eq(p->as.struct_.fields.items[f].name, name)) {
            return p->as.struct_.fields.items[f].pattern;
        }
    }
    return NULL;
}

// Field `i` of a constructor pattern; NULL (a wildcard) when not written.
static const Pattern *field(Ctx *x, const Pattern *p, uint32_t i) {
    if (!p || head(p) == WILD) {
        return NULL;
    }
    switch (p->kind) {
    case PAT_VARIANT: return i < p->as.variant.args.len ? p->as.variant.args.items[i] : NULL;
    case PAT_TUPLE: return i < p->as.tuple.len ? p->as.tuple.items[i] : NULL;
    case PAT_TUPLE_STRUCT:
        return i < p->as.tuple_struct.args.len ? p->as.tuple_struct.args.items[i] : NULL;
    case PAT_STRUCT: return struct_field(x, p, i);
    default: return NULL;   // GCOVR_EXCL_LINE: only constructors with fields are asked for one
    }
}

// ---------------------------------------------------------------------------
// Matrices

static void push_row(Ctx *x, Matrix *m, Row row) {
    const Pattern *first = row.len ? row.items[0] : NULL;
    if (first && first->kind == PAT_OR) {   // one row per alternative
        for (uint32_t i = 0; i < first->as.alternatives.len; i++) {
            const Pattern **items = ARENA_ARRAY(x->arena, const Pattern *, row.len);
            memcpy(items, row.items, row.len * sizeof *items);
            items[0] = first->as.alternatives.items[i];
            push_row(x, m, (Row){items, row.len});
        }
        return;
    }
    if (m->len == m->cap) {
        uint32_t cap = m->cap ? m->cap * 2 : 8;
        Row *rows = ARENA_ARRAY(x->arena, Row, cap);
        if (m->len) {
            memcpy(rows, m->rows, m->len * sizeof *rows);
        }
        m->rows = rows;
        m->cap = cap;
    }
    m->rows[m->len++] = row;
}

// The row for constructor `ctor` of arity `n`: the head's fields, then the
// rest. A wildcard head stands for `n` wildcards.
static Row specialize_row(Ctx *x, Row row, uint32_t n) {
    Row out = {ARENA_ARRAY(x->arena, const Pattern *, row.len + n), row.len - 1 + n};
    for (uint32_t i = 0; i < n; i++) {
        out.items[i] = field(x, row.items[0], i);
    }
    for (uint32_t i = 1; i < row.len; i++) {
        out.items[n + i - 1] = row.items[i];
    }
    return out;
}

static Matrix specialize(Ctx *x, Matrix m, int64_t ctor, uint32_t n) {
    Matrix out = {0};
    for (uint32_t r = 0; r < m.len; r++) {
        int64_t h = head(m.rows[r].items[0]);
        if (h == WILD || (h == ctor && h != OPAQUE)) {
            push_row(x, &out, specialize_row(x, m.rows[r], n));
        }
    }
    return out;
}

// The rows whose head is a wildcard, without it.
static Matrix default_rows(Ctx *x, Matrix m) {
    Matrix out = {0};
    for (uint32_t r = 0; r < m.len; r++) {
        if (head(m.rows[r].items[0]) == WILD) {
            push_row(x, &out, (Row){m.rows[r].items + 1, m.rows[r].len - 1});
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// Witnesses: the text of a value that no row matches, one entry per column.

static const char *copy_text(Ctx *x, const StrBuf *text) {
    char *out = ARENA_ARRAY(x->arena, char, text->len + 1);
    if (text->len > 0) {   // an empty buffer may have no data
        memcpy(out, text->data, text->len);
    }
    out[text->len] = '\0';
    return out;
}

// The text of constructor `ctor` of `type` applied to `fields`.
static const char *ctor_text(Ctx *x, TypeId type, uint32_t ctor, const char **fields,
                             uint32_t n) {
    const Ty *ty = type_get(x->c->types, type);
    StrBuf text = {0};
    if (ty->kind == TY_BOOL) {
        strbuf_append_cstr(&text, ctor ? "true" : "false");
    } else if (ty->kind == TY_ADT) {
        const Def *def = sema_def(x->c->sema, (DefId){ty->data});
        Symbol name = def->name;
        if (def->kind == DEF_ENUM) {   // `Shape.Empty` (ADR 0028)
            strbuf_append(&text, symbol_name(&x->c->sema->session->names, name));
            strbuf_append_cstr(&text, ".");
            name = def->variants->variants[ctor].name;
        }
        strbuf_append(&text, symbol_name(&x->c->sema->session->names, name));
        bool named = def->kind == DEF_STRUCT && !def->fields->is_tuple;
        if (named) {
            strbuf_append_cstr(&text, n ? "{..}" : "{}");
        }
        n = named ? 0 : n;
    }
    if (n > 0 || ty->kind == TY_TUPLE || ty->kind == TY_UNIT) {
        strbuf_push(&text, '(');
        for (uint32_t i = 0; i < n; i++) {
            strbuf_append_cstr(&text, i ? ", " : "");
            strbuf_append_cstr(&text, fields[i]);
        }
        strbuf_push(&text, ')');
    }
    const char *out = copy_text(x, &text);
    strbuf_free(&text);
    return out;
}

static const char **useful(Ctx *x, Matrix m, Row q, const TypeId *types);

// `q` with the constructor `ctor` at its head: its fields join the columns.
static const char **useful_ctor(Ctx *x, Matrix m, Row q, const TypeId *types, int64_t ctor) {
    uint32_t n = arity(x, types[0], (uint32_t)ctor);
    TypeId *sub = ARENA_ARRAY(x->arena, TypeId, n + q.len);
    for (uint32_t i = 0; i < n; i++) {
        sub[i] = field_type(x, types[0], (uint32_t)ctor, i);
    }
    memcpy(sub + n, types + 1, (q.len - 1) * sizeof *types);
    const char **w = useful(x, specialize(x, m, ctor, n), specialize_row(x, q, n), sub);
    if (!w) {
        return NULL;
    }
    const char **out = ARENA_ARRAY(x->arena, const char *, q.len);
    out[0] = ctor_text(x, types[0], (uint32_t)ctor, w, n);
    memcpy(out + 1, w + n, (q.len - 1) * sizeof *out);
    return out;
}

// `q` with a wildcard at its head.
static const char **useful_wild(Ctx *x, Matrix m, Row q, const TypeId *types) {
    uint32_t count = ctor_count(x, types[0]);
    bool *present = ARENA_ARRAY(x->arena, bool, count + 1);
    uint32_t seen = 0;
    for (uint32_t r = 0; r < m.len; r++) {
        int64_t h = head(m.rows[r].items[0]);
        if (h >= 0 && (uint32_t)h < count && !present[h]) {
            present[h] = true;
            seen++;
        }
    }
    if (count > 0 && seen == count) {   // complete: some constructor must be useful
        for (uint32_t ctor = 0; ctor < count; ctor++) {
            const char **w = useful_ctor(x, m, q, types, ctor);
            if (w) {
                return w;
            }
        }
        return NULL;
    }
    const char **w = useful(x, default_rows(x, m), (Row){q.items + 1, q.len - 1}, types + 1);
    if (!w) {
        return NULL;
    }
    const char **out = ARENA_ARRAY(x->arena, const char *, q.len);
    out[0] = "_";
    for (uint32_t ctor = 0; seen > 0 && ctor < count; ctor++) {
        if (!present[ctor]) {   // a missing constructor, with wildcard fields
            uint32_t n = arity(x, types[0], ctor);
            const char **fields = ARENA_ARRAY(x->arena, const char *, n + 1);
            for (uint32_t i = 0; i < n; i++) {
                fields[i] = "_";
            }
            out[0] = ctor_text(x, types[0], ctor, fields, n);
            break;
        }
    }
    memcpy(out + 1, w, (q.len - 1) * sizeof *out);
    return out;
}

// A witness that `q` is useful after `m`, or NULL.
static const char **useful(Ctx *x, Matrix m, Row q, const TypeId *types) {
    if (q.len == 0) {
        return m.len == 0 ? ARENA_ARRAY(x->arena, const char *, 1) : NULL;
    }
    const Pattern *first = q.items[0];
    if (first && first->kind == PAT_OR) {
        for (uint32_t i = 0; i < first->as.alternatives.len; i++) {
            Row alt = {ARENA_ARRAY(x->arena, const Pattern *, q.len), q.len};
            memcpy(alt.items, q.items, q.len * sizeof *alt.items);
            alt.items[0] = first->as.alternatives.items[i];
            const char **w = useful(x, m, alt, types);
            if (w) {
                return w;
            }
        }
        return NULL;
    }
    int64_t h = head(first);
    if (h == WILD) {
        return useful_wild(x, m, q, types);
    }
    if (h == OPAQUE) {   // a literal or range: only the wildcard rows surely cover it
        const char **w = useful(x, default_rows(x, m), (Row){q.items + 1, q.len - 1},
                                types + 1);
        if (!w) {
            return NULL;
        }
        const char **out = ARENA_ARRAY(x->arena, const char *, q.len);
        out[0] = "_";
        memcpy(out + 1, w, (q.len - 1) * sizeof *out);
        return out;
    }
    return useful_ctor(x, m, q, types, h);
}

// ---------------------------------------------------------------------------

void check_exhaustive(Checker *c, Expr *e, TypeId type) {
    if (type.id == TID_ERROR) {
        return;
    }
    Ctx x = {c, &c->sema->session->arena};
    Matrix m = {0};
    for (uint32_t i = 0; i < e->as.match.arms.len; i++) {
        const MatchArm *arm = &e->as.match.arms.items[i];
        const Pattern **item = ARENA_NEW(x.arena, const Pattern *);
        *item = arm->pattern;
        Row row = {item, 1};
        if (!useful(&x, m, row, &type)) {
            diag_warning(&c->sema->session->diags, "W0004", arm->pattern->span,
                         "unreachable `match` arm: the arms before it match every value");
        }
        if (!arm->guard) {
            push_row(&x, &m, row);
        }
    }
    const Pattern **wild = ARENA_NEW(x.arena, const Pattern *);
    const char **w = useful(&x, m, (Row){wild, 1}, &type);
    if (w) {
        check_error(c, "E0500", e->span, "non-exhaustive `match`: `%s` is not covered", w[0]);
    }
}
