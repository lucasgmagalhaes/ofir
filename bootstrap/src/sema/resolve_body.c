// Name resolution, second pass: function bodies. Binds each identifier to a
// local or a definition and gives every binding a LocalId.
//
// Parameters and the top level of the body are one scope (spec 3.1: a name
// cannot be declared twice in the same scope); each block, loop and match
// arm opens a nested one.

#include "sema/resolve_internal.h"
#include "sema/suggest.h"

#include <string.h>

static void resolve_block(Resolver *r, Block *block);
static void resolve_stmts(Resolver *r, StmtList stmts);

static LocalId declare_local(Resolver *r, Symbol name, Span span, bool mutable) {
    Scope *scope = &r->sema->scope;
    if (scope_in_current_block(scope, name)) {
        resolve_error(r, "E0201", span, "`" SV_FMT "` is defined more than once in this scope",
                      SV_ARG(resolver_name(r, name)));
    }
    LocalId id = {(uint32_t)r->locals.len};
    trace(r, span, "local %u " SV_FMT, id.id, SV_ARG(resolver_name(r, name)));
    VEC_PUSH(r->locals, ((Local){.name = name, .span = span, .mutable = mutable}));
    scope_declare(scope, name, id);
    return id;
}

static Res find(Resolver *r, Symbol name, Span span);

static Res lookup(Resolver *r, Symbol name, Span span) {
    Res res = find(r, name, span);
    trace_use(r, "use", name, span, res);
    return res;
}

static Res find(Resolver *r, Symbol name, Span span) {
    LocalId local;
    if (scope_local(&r->sema->scope, name, &local)) {
        r->locals.data[local.id].used = true;
        return (Res){RES_LOCAL, local.id};
    }
    DefId def = scope_module(&r->sema->scope, name);
    if (def.id != 0) {
        const Def *found = sema_def(r->sema, def);
        if (found->module != r->sema->current || found->kind == DEF_MODULE) {   // maybe imported
            note_import_use(r->sema, name);
        }
        return (Res){RES_DEF, def.id};
    }
    resolve_error(r, "E0200", span, "cannot find `" SV_FMT "` in this scope",
                  SV_ARG(resolver_name(r, name)));
    if (sv_eq(resolver_name(r, name), SV("assert"))) {   // no longer built in (ADR 0036)
        diag_help(&r->sema->session->diags, "import it: `import std.testing.assert`");
        return (Res){RES_ERROR, 0};
    }
    suggest_name(r->sema, name, SUGGEST_VALUE);
    return (Res){RES_ERROR, 0};
}

// ---------------------------------------------------------------------------
// Patterns

static Pattern *binding_pattern(Resolver *r, Symbol name, Span span) {
    Pattern *p = ARENA_NEW(&r->sema->session->arena, Pattern);
    p->kind = PAT_BINDING;
    p->span = span;
    p->as.binding.name = name;
    return p;
}

static void declare_patterns(Resolver *r, PatternList list, bool reuse);

// `None` and `Some(x)` in a pattern name the prelude's variants, not a new
// binding or a tuple struct: they become variant patterns.
static void prelude_variant(Resolver *r, Pattern *p) {
    Symbol name = p->kind == PAT_BINDING ? p->as.binding.name
                  : p->kind == PAT_TUPLE_STRUCT && p->as.tuple_struct.path.len == 1
                      ? p->as.tuple_struct.path.items[0]
                      : (Symbol){0};
    DefId def = name.id ? scope_module(&r->sema->scope, name) : (DefId){0};
    if (def.id == 0 || sema_def(r->sema, def)->kind != DEF_VARIANT) {
        return;
    }
    trace_use(r, "variant", name, p->span, (Res){RES_DEF, def.id});
    if (p->kind == PAT_BINDING && p->as.binding.mutable) {
        resolve_error(r, "E0200", p->span, "`" SV_FMT "` is a variant, not a variable",
                      SV_ARG(resolver_name(r, name)));
    }
    PatternList args = p->kind == PAT_TUPLE_STRUCT ? p->as.tuple_struct.args : (PatternList){0};
    bool has_args = p->kind == PAT_TUPLE_STRUCT;
    p->kind = PAT_VARIANT;
    p->as.variant.path = (SymbolList){0};
    p->as.variant.name = name;
    p->as.variant.args = args;
    p->as.variant.has_args = has_args;
}

// Declares the bindings of a pattern. With `reuse`, a name already bound in
// this block keeps its local: the alternatives of `a | b` bind the same names.
static void declare_pattern(Resolver *r, Pattern *p, bool reuse) {
    prelude_variant(r, p);
    switch (p->kind) {
    case PAT_BINDING: {
        Symbol name = p->as.binding.name;
        LocalId existing;
        if (reuse && scope_in_current_block(&r->sema->scope, name) &&
            scope_local(&r->sema->scope, name, &existing)) {
            p->as.binding.local = existing;
        } else {
            p->as.binding.local = declare_local(r, name, p->span, p->as.binding.mutable);
        }
        break;
    }
    case PAT_LITERAL: resolve_expr(r, p->as.literal); break;
    case PAT_RANGE:
        resolve_expr(r, p->as.range.lo);
        resolve_expr(r, p->as.range.hi);
        break;
    case PAT_VARIANT: declare_patterns(r, p->as.variant.args, reuse); break;
    case PAT_TUPLE: declare_patterns(r, p->as.tuple, reuse); break;
    case PAT_TUPLE_STRUCT: declare_patterns(r, p->as.tuple_struct.args, reuse); break;
    case PAT_STRUCT:
        for (uint32_t i = 0; i < p->as.struct_.fields.len; i++) {
            FieldPattern *f = &p->as.struct_.fields.items[i];
            if (!f->pattern) {   // `Point{x}` binds `x`
                f->pattern = binding_pattern(r, f->name, p->span);
            }
            declare_pattern(r, f->pattern, reuse);
        }
        break;
    case PAT_OR:
        for (uint32_t i = 0; i < p->as.alternatives.len; i++) {
            declare_pattern(r, p->as.alternatives.items[i], reuse || i > 0);
        }
        break;
    case PAT_WILDCARD:
        break;
    }
}

static void declare_patterns(Resolver *r, PatternList list, bool reuse) {
    for (uint32_t i = 0; i < list.len; i++) {
        declare_pattern(r, list.items[i], reuse);
    }
}

// ---------------------------------------------------------------------------
// Expressions

static void resolve_exprs(Resolver *r, ExprList list) {
    for (uint32_t i = 0; i < list.len; i++) {
        resolve_expr(r, list.items[i]);
    }
}

static void resolve_args(Resolver *r, ArgList args) {
    for (uint32_t i = 0; i < args.len; i++) {
        resolve_expr(r, args.items[i].value);
    }
}

static void resolve_struct_literal(Resolver *r, Expr *e) {
    resolve_expr(r, e->as.struct_.type);
    for (uint32_t i = 0; i < e->as.struct_.fields.len; i++) {
        FieldInit *f = &e->as.struct_.fields.items[i];
        if (!f->value) {   // `Point{x}` is `Point{x: x}`
            f->value = ARENA_NEW(&r->sema->session->arena, Expr);
            f->value->kind = EXPR_IDENT;
            f->value->span = e->span;
            f->value->as.ident.name = f->name;
        }
        resolve_expr(r, f->value);
    }
    if (e->as.struct_.base) {
        resolve_expr(r, e->as.struct_.base);
    }
}

static void resolve_if(Resolver *r, Expr *e) {
    resolve_expr(r, e->as.if_.cond);
    scope_enter(&r->sema->scope);   // `if const` bindings are visible in `then` only
    if (e->as.if_.let_pattern) {
        declare_pattern(r, e->as.if_.let_pattern, false);
    }
    resolve_block(r, e->as.if_.then);
    scope_leave(&r->sema->scope);
    if (e->as.if_.else_) {
        resolve_expr(r, e->as.if_.else_);
    }
}

static void resolve_match(Resolver *r, Expr *e) {
    resolve_expr(r, e->as.match.scrutinee);
    for (uint32_t i = 0; i < e->as.match.arms.len; i++) {
        MatchArm *arm = &e->as.match.arms.items[i];
        scope_enter(&r->sema->scope);
        declare_pattern(r, arm->pattern, false);
        if (arm->guard) {
            resolve_expr(r, arm->guard);
        }
        resolve_expr(r, arm->body);
        scope_leave(&r->sema->scope);
    }
}

static void resolve_self(Resolver *r, Expr *e) {
    LocalId local;
    if (scope_local(&r->sema->scope, r->self_name, &local)) {
        e->as.ident.res = (Res){RES_LOCAL, local.id};
        trace_use(r, "use", r->self_name, e->span, e->as.ident.res);
    } else {
        resolve_error(r, "E0200", e->span, "`self` is only available in methods");
        e->as.ident.res = (Res){RES_ERROR, 0};
    }
}

// `Self` in a method names the enclosing type: it becomes that name.
static void resolve_self_type(Resolver *r, Expr *e) {
    if (!r->owner) {
        resolve_error(r, "E0200", e->span, "`Self` is only available inside a type");
        e->kind = EXPR_IDENT;
        e->as.ident.res = (Res){RES_ERROR, 0};
        return;
    }
    e->kind = EXPR_IDENT;
    e->as.ident.name = r->owner->name;
    e->as.ident.res = (Res){RES_DEF, scope_module(&r->sema->scope, r->owner->name).id};
    trace_use(r, "use Self as", r->owner->name, e->span, e->as.ident.res);
}

void resolve_expr(Resolver *r, Expr *e) {
    switch (e->kind) {
    case EXPR_INT: case EXPR_FLOAT: case EXPR_BYTE: case EXPR_STR: case EXPR_BOOL:
        break;
    case EXPR_SELF_TYPE: resolve_self_type(r, e); break;
    case EXPR_INTERP: resolve_exprs(r, e->as.parts); break;
    case EXPR_IDENT: e->as.ident.res = lookup(r, e->as.ident.name, e->span); break;
    case EXPR_SELF: resolve_self(r, e); break;
    case EXPR_GENERIC: e->as.generic.res = lookup(r, e->as.generic.name, e->span); break;
    case EXPR_UNARY: resolve_expr(r, e->as.unary.operand); break;
    case EXPR_BINARY:
        resolve_expr(r, e->as.binary.lhs);
        resolve_expr(r, e->as.binary.rhs);
        break;
    case EXPR_CAST: resolve_expr(r, e->as.cast.value); break;
    case EXPR_RANGE:
        if (e->as.range.lo) resolve_expr(r, e->as.range.lo);
        if (e->as.range.hi) resolve_expr(r, e->as.range.hi);
        break;
    case EXPR_CALL:
        resolve_expr(r, e->as.call.callee);
        resolve_args(r, e->as.call.args);
        break;
    case EXPR_INDEX:
        resolve_expr(r, e->as.index.base);
        resolve_expr(r, e->as.index.index);
        break;
    case EXPR_FIELD:   // a field or method name needs types; a module member is resolved now
        resolve_expr(r, e->as.field.base);
        resolve_qualified(r, e);
        break;
    case EXPR_TUPLE_FIELD: resolve_expr(r, e->as.tuple_field.base); break;
    case EXPR_TRY: resolve_expr(r, e->as.inner); break;
    case EXPR_STRUCT: resolve_struct_literal(r, e); break;
    case EXPR_ARRAY: case EXPR_TUPLE: resolve_exprs(r, e->as.elems); break;
    case EXPR_ARRAY_REPEAT:
        resolve_expr(r, e->as.repeat.value);
        resolve_expr(r, e->as.repeat.count);
        break;
    case EXPR_VARIANT: resolve_args(r, e->as.variant.args); break;
    case EXPR_IF: resolve_if(r, e); break;
    case EXPR_MATCH: resolve_match(r, e); break;
    case EXPR_BLOCK: case EXPR_UNSAFE: resolve_block(r, e->as.block); break;
    }
}

// ---------------------------------------------------------------------------
// Statements

static void resolve_let(Resolver *r, Stmt *s) {
    resolve_expr(r, s->as.let.value);   // `const x = x + 1` reads the outer `x`
    if (s->as.let.else_) {
        resolve_block(r, s->as.let.else_);
    }
    declare_pattern(r, s->as.let.pattern, false);
}

static void resolve_for(Resolver *r, Stmt *s) {
    if (s->as.for_.expr) {
        resolve_expr(r, s->as.for_.expr);
    }
    scope_enter(&r->sema->scope);
    if (s->as.for_.kind == FOR_IN) {
        bool mutable = s->as.for_.mutable;
        if (s->as.for_.second.id != 0) {
            s->as.for_.first_local = declare_local(r, s->as.for_.first, s->span, false);
            s->as.for_.second_local = declare_local(r, s->as.for_.second, s->span, mutable);
        } else {
            s->as.for_.first_local = declare_local(r, s->as.for_.first, s->span, mutable);
        }
    }
    resolve_block(r, s->as.for_.body);
    scope_leave(&r->sema->scope);
}

static void resolve_stmt(Resolver *r, Stmt *s) {
    switch (s->kind) {
    case STMT_LET: resolve_let(r, s); break;
    case STMT_VAR:
        if (s->as.var.value) {
            resolve_expr(r, s->as.var.value);
        }
        s->as.var.local = declare_local(r, s->as.var.name, s->span, true);
        break;
    case STMT_ASSIGN:
        resolve_expr(r, s->as.assign.target);
        resolve_expr(r, s->as.assign.value);
        break;
    case STMT_FOR: resolve_for(r, s); break;
    case STMT_RETURN: case STMT_EXPR:
        if (s->as.expr) {
            resolve_expr(r, s->as.expr);
        }
        break;
    case STMT_BREAK: case STMT_CONTINUE:
        break;
    }
}

static void resolve_stmts(Resolver *r, StmtList stmts) {
    for (uint32_t i = 0; i < stmts.len; i++) {
        resolve_stmt(r, stmts.items[i]);
    }
}

static void resolve_block(Resolver *r, Block *block) {
    scope_enter(&r->sema->scope);
    resolve_stmts(r, block->stmts);
    scope_leave(&r->sema->scope);
}

// W0001 for the variables never read; parameters and names starting with
// `_` are exempt (spec 12.6).
static void warn_unused_locals(Resolver *r, const FnDecl *fn) {
    uint32_t params = fn->params.len + (fn->has_self ? 1 : 0);
    for (size_t i = params; i < r->locals.len; i++) {
        const Local *local = &r->locals.data[i];
        StrView name = resolver_name(r, local->name);
        if (!local->used && name.len > 0 && name.ptr[0] != '_') {
            diag_warning(&r->sema->session->diags, "W0001", local->span,
                         "unused variable `" SV_FMT "`: use it, or start its name with `_`",
                         SV_ARG(name));
        }
    }
}

void resolve_fn(Resolver *r, FnDecl *fn) {
    Scope *scope = &r->sema->scope;
    if (r->owner) {
        trace(r, fn->span, "fn " SV_FMT "." SV_FMT, SV_ARG(resolver_name(r, r->owner->name)),
              SV_ARG(resolver_name(r, fn->name)));
    } else {
        trace(r, fn->span, "fn " SV_FMT, SV_ARG(resolver_name(r, fn->name)));
    }
    scope_reset_locals(scope);
    r->locals.len = 0;
    scope_enter(scope);
    if (fn->has_self) {
        declare_local(r, r->self_name, fn->span, fn->self_mode == MODE_MUT);
    }
    for (uint32_t i = 0; i < fn->params.len; i++) {
        Param *param = &fn->params.items[i];
        if (param->default_value) {
            resolve_expr(r, param->default_value);
        }
        param->local = declare_local(r, param->name, param->span, param->mode == MODE_MUT);
    }
    if (fn->body) {
        resolve_stmts(r, fn->body->stmts);   // same scope as the parameters
    }
    scope_leave(scope);
    warn_unused_locals(r, fn);
    fn->locals.len = (uint32_t)r->locals.len;
    fn->locals.items = ARENA_ARRAY(&r->sema->session->arena, Local, r->locals.len + 1);
    if (r->locals.len > 0) {
        memcpy(fn->locals.items, r->locals.data, r->locals.len * sizeof(Local));
    }
}
