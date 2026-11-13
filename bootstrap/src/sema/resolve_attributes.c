// Attribute checks (spec 12.1): known names, where each one applies, its
// arguments, and those stage 0 does not implement yet. Stage 0 accepts
// `@deprecated` and `@allow` and ignores them (stage 1 implements them).

#include "sema/resolve_internal.h"

enum {
    ON_FN = 1 << 0,          // a top-level function
    ON_METHOD = 1 << 1,
    ON_EXTERN_FN = 1 << 2,   // a function of an `extern` block
    ON_STRUCT = 1 << 3,
    ON_ENUM = 1 << 4,
    ON_FIELD = 1 << 5,
    ON_EXTERN = 1 << 6,      // an `extern` block
    ON_CONST = 1 << 7,
    ON_GLOBAL = 1 << 8,
    ON_ALIAS = 1 << 9,
    ON_IMPORT = 1 << 10,
    ON_ANY = (1 << 11) - 1,
};

typedef enum {
    ARGS_NONE,       // @cold
    ARGS_INLINE,     // @inline, @inline(never)
    ARGS_REPR,       // @repr(C)
    ARGS_ALIGN,      // @align(8)
    ARGS_STR,        // @link("m")
    ARGS_OPT_STR,    // @deprecated, @deprecated("use g")
    ARGS_ONE,        // @cfg(debug)
    ARGS_ALLOW,      // @allow(unused)
    ARGS_TWO_STR,    // @tag("key", "value")
} ArgsKind;

typedef struct {
    const char *name;
    int targets;
    ArgsKind args;
    bool supported;   // implemented by stage 0
    const char *usage;
} AttributeRule;

static const AttributeRule RULES[] = {
    {"inline", ON_FN | ON_METHOD, ARGS_INLINE, true, "`@inline` or `@inline(never)`"},
    {"cold", ON_FN | ON_METHOD, ARGS_NONE, true, "`@cold`"},
    {"test", ON_FN, ARGS_NONE, true, "`@test`"},
    {"lang", ON_STRUCT | ON_ENUM, ARGS_STR, true, "`@lang(\"type\")`"},
    {"repr", ON_STRUCT | ON_ENUM, ARGS_REPR, false, "`@repr(C)`"},
    {"packed", ON_STRUCT, ARGS_NONE, false, "`@packed`"},
    {"align", ON_STRUCT, ARGS_ALIGN, false, "`@align(N)`, N a power of two up to 4096"},
    {"link", ON_EXTERN, ARGS_STR, false, "`@link(\"name\")`"},
    {"export", ON_FN, ARGS_STR, false, "`@export(\"name\")`"},
    {"deprecated", ON_ANY, ARGS_OPT_STR, true,
     "`@deprecated` or `@deprecated(\"message\")`"},
    {"bench", ON_FN, ARGS_NONE, false, "`@bench`"},
    {"cfg", ON_ANY, ARGS_ONE, false, "`@cfg(condition)`"},
    {"allow", ON_ANY, ARGS_ALLOW, true,
     "`@allow(category)`, category `unused`, `unreachable`, `deprecated` or `naming`"},
    {"tag", ON_STRUCT | ON_ENUM | ON_FIELD, ARGS_TWO_STR, false, "`@tag(\"key\", \"value\")`"},
    // Stage 0 has no tasks, so no `Send` to check: it accepts the marker.
    {"threadsafe", ON_STRUCT | ON_ENUM, ARGS_NONE, true, "`@threadsafe`"},
    {"threadLocal", ON_GLOBAL, ARGS_NONE, true, "`@threadLocal`"},
};

static const char *target_name(int target) {
    switch (target) {
    case ON_FN: return "a function";
    case ON_METHOD: return "a method";
    case ON_EXTERN_FN: return "a function of an extern block";
    case ON_STRUCT: return "a struct";
    case ON_ENUM: return "an enum";
    case ON_FIELD: return "a field";
    case ON_EXTERN: return "an extern block";
    case ON_CONST: return "a constant";
    case ON_GLOBAL: return "a global variable";
    case ON_ALIAS: return "a type alias";
    default: return "an import";
    }
}

static bool is_ident(const Resolver *r, const Expr *e, const char *name) {
    return e->kind == EXPR_IDENT &&
           (!name || sv_eq(resolver_name(r, e->as.ident.name), sv_from_cstr(name)));
}

static bool args_match(const Resolver *r, ArgsKind kind, const Attribute *a) {
    uint32_t n = a->args.len;
    const Expr *first = n > 0 ? a->args.items[0] : NULL;
    switch (kind) {
    case ARGS_NONE: return n == 0;
    case ARGS_INLINE: return n == 0 || (n == 1 && is_ident(r, first, "never"));
    case ARGS_REPR: return n == 1 && is_ident(r, first, "C");
    case ARGS_ALIGN:
        return n == 1 && first->kind == EXPR_INT && first->as.int_value > 0 &&
               first->as.int_value <= 4096 &&
               (first->as.int_value & (first->as.int_value - 1)) == 0;
    case ARGS_STR: return n == 1 && first->kind == EXPR_STR;
    case ARGS_OPT_STR: return n == 0 || (n == 1 && first->kind == EXPR_STR);
    case ARGS_ONE: return n == 1;
    case ARGS_ALLOW:
        return n == 1 && (is_ident(r, first, "unused") || is_ident(r, first, "unreachable") ||
                          is_ident(r, first, "deprecated") || is_ident(r, first, "naming"));
    case ARGS_TWO_STR:
        return n == 2 && first->kind == EXPR_STR && a->args.items[1]->kind == EXPR_STR;
    }
    return false;
}

static const AttributeRule *rule_of(StrView name) {
    for (size_t i = 0; i < sizeof RULES / sizeof RULES[0]; i++) {
        if (sv_eq(name, sv_from_cstr(RULES[i].name))) {
            return &RULES[i];
        }
    }
    return NULL;
}

static void check_list(Resolver *r, AttributeList attributes, int target) {
    for (uint32_t i = 0; i < attributes.len; i++) {
        const Attribute *a = &attributes.items[i];
        StrView name = resolver_name(r, a->name);
        const AttributeRule *rule = rule_of(name);
        bool twice = false;
        for (uint32_t j = 0; j < i; j++) {
            twice = twice || attributes.items[j].name.id == a->name.id;
        }
        if (!rule) {
            resolve_error(r, "E0807", a->span, "unknown attribute `@" SV_FMT "`", SV_ARG(name));
        } else if (twice) {
            resolve_error(r, "E0810", a->span, "the attribute `@%s` is written twice",
                          rule->name);
        } else if (!(rule->targets & target)) {
            resolve_error(r, "E0808", a->span, "the attribute `@%s` does not apply to %s",
                          rule->name, target_name(target));
        } else if (!args_match(r, rule->args, a)) {
            resolve_error(r, "E0809", a->span, "invalid arguments for `@%s`: write %s",
                          rule->name, rule->usage);
        } else if (!rule->supported) {
            resolve_error(r, "E0901", a->span,
                          "the attribute `@%s` is not supported by stage 0", rule->name);
        }
    }
}

static void check_fields(Resolver *r, const Item *item) {
    for (uint32_t i = 0; i < item->as.struct_.fields.len; i++) {
        check_list(r, item->as.struct_.fields.items[i].attributes, ON_FIELD);
    }
}

static void check_fns(Resolver *r, FnList fns, int target) {
    for (uint32_t i = 0; i < fns.len; i++) {
        check_list(r, fns.items[i]->attributes, target);
    }
}

void check_attributes(Resolver *r, const Item *item) {
    switch (item->kind) {
    case ITEM_FN: check_list(r, item->attributes, ON_FN); break;
    case ITEM_STRUCT: case ITEM_TUPLE_STRUCT:
        check_list(r, item->attributes, ON_STRUCT);
        check_fields(r, item);
        check_fns(r, item->as.struct_.methods, ON_METHOD);
        break;
    case ITEM_ENUM:
        check_list(r, item->attributes, ON_ENUM);
        check_fns(r, item->as.enum_.methods, ON_METHOD);
        break;
    case ITEM_CONST: check_list(r, item->attributes, ON_CONST); break;
    case ITEM_GLOBAL: check_list(r, item->attributes, ON_GLOBAL); break;
    case ITEM_TYPE_ALIAS: check_list(r, item->attributes, ON_ALIAS); break;
    case ITEM_IMPORT: check_list(r, item->attributes, ON_IMPORT); break;
    case ITEM_EXTERN:
        check_list(r, item->attributes, ON_EXTERN);
        check_fns(r, item->as.extern_.fns, ON_EXTERN_FN);
        break;
    }
}
