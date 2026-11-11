// Items and modules (spec appendix A.1 and A.3), and the parser entry point.

#include "syntax/parser_internal.h"

static Attribute attribute(Parser *p) {
    Attribute a = {.span = advance(p)->span};   // @
    const Token *name = expect(p, TOK_IDENT, "an attribute name after `@`");
    a.name = name ? name->as.symbol : (Symbol){0};
    Vec(Expr *) args = {0};
    if (accept(p, TOK_LPAREN)) {
        while (!at(p, TOK_RPAREN) && !at(p, TOK_EOF)) {
            VEC_PUSH(args, parse_expr(p));
            if (!accept(p, TOK_COMMA)) {
                break;
            }
        }
        expect(p, TOK_RPAREN, "`)` to close the attribute arguments");
    }
    TO_LIST(p, args, a.args);
    a.span = span_since(p, a.span);
    skip_newlines(p);
    return a;
}

ItemHeader parse_item_header(Parser *p) {
    ItemHeader h = {.span = peek_token(p, 0)->span};
    Vec(Attribute) attrs = {0};
    while (at(p, TOK_AT)) {
        VEC_PUSH(attrs, attribute(p));
    }
    TO_LIST(p, attrs, h.attributes);
    h.is_pub = accept(p, TOK_KW_PUB);
    return h;
}

static void import(Parser *p, Item *item) {
    Vec(Symbol) path = {0};
    do {
        const Token *t = expect(p, TOK_IDENT, "a module name");
        if (!t) {
            break;
        }
        VEC_PUSH(path, t->as.symbol);
    } while (accept(p, TOK_DOT));
    if (accept(p, TOK_KW_AS)) {
        const Token *alias = expect(p, TOK_IDENT, "a name after `as`");
        item->as.import.alias = alias ? alias->as.symbol : (Symbol){0};
    }
    TO_LIST(p, path, item->as.import.path);
}

// `const NAME: T = value` and the global `let NAME: T = value`.
static void value_item(Parser *p, Item *item) {
    const Token *name = expect(p, TOK_IDENT, "a name");
    item->name = name ? name->as.symbol : (Symbol){0};
    if (accept(p, TOK_COLON)) {
        item->as.const_.type = parse_type(p);
    }
    if (expect(p, TOK_EQ, "`=` and a value")) {
        item->as.const_.value = parse_expr(p);
    }
}

// True when the alias body after `=` is a mapped or conditional type
// (spec 16.4, 16.5): `{ [K in ...]: ... }` or `if A is B { X } else { Y }`.
static bool at_type_operator(const Parser *p) {
    for (size_t ahead = 0;; ahead++) {
        TokenKind k = peek_at(p, ahead);
        if (k == TOK_NEWLINE || k == TOK_EOF) {
            return false;
        }
        if (k == TOK_EQ) {
            TokenKind body = peek_at(p, ahead + 1);
            return body == TOK_LBRACE || body == TOK_KW_IF;
        }
    }
}

static void type_alias(Parser *p, Item *item) {
    const Token *name = expect(p, TOK_IDENT, "a type name");
    item->name = name ? name->as.symbol : (Symbol){0};
    if (at_type_operator(p)) {
        // Stage 0 keeps a type operator alias opaque: a use of it is E0901.
        int depth = 0;
        while (!at(p, TOK_EOF) && (depth > 0 || !at(p, TOK_NEWLINE))) {
            TokenKind k = advance(p)->kind;
            depth += k == TOK_LBRACE || k == TOK_LBRACKET || k == TOK_LPAREN;
            depth -= (k == TOK_RBRACE || k == TOK_RBRACKET || k == TOK_RPAREN) && depth > 0;
        }
        return;
    }
    WhereVec bounds = {0};
    item->as.alias.type_params = parse_type_params(p, &bounds);
    if (bounds.len) {
        syntax_error(p, bounds.data[0].span, "a type alias cannot bound its type parameters");
    }
    VEC_FREE(bounds);
    expect(p, TOK_EQ, "`=` and a type");
    item->as.alias.type = parse_type(p);
}

// `extern "C" { fn ... }` or `extern "C" fn name(...) { body }`.
static void extern_item(Parser *p, Item *item, ItemHeader header) {
    const Token *abi = expect(p, TOK_STR, "the ABI string, as in `extern \"C\"`");
    StrView abi_text = abi ? abi->as.text : SV("C");
    if (accept(p, TOK_KW_FN)) {
        item->kind = ITEM_FN;
        item->as.fn = parse_fn(p, header, true);
        item->as.fn->abi = abi_text;
        return;
    }
    item->as.extern_.abi = abi_text;
    Vec(FnDecl *) fns = {0};
    expect(p, TOK_LBRACE, "`{` and the declarations");
    skip_newlines(p);
    while (!at(p, TOK_RBRACE) && !at(p, TOK_EOF) && !p->recovering) {
        ItemHeader fn_header = parse_item_header(p);
        expect(p, TOK_KW_FN, "`fn`");
        VEC_PUSH(fns, parse_fn(p, fn_header, false));
        expect_terminator(p);
    }
    expect(p, TOK_RBRACE, "`}` to close the extern block");
    TO_LIST(p, fns, item->as.extern_.fns);
}

static void item_body(Parser *p, Item *item, ItemHeader header) {
    bool is_copy = accept(p, TOK_KW_COPY);
    bool is_ref = !is_copy && at_word(p, p->words.ref) && peek_at(p, 1) == TOK_KW_STRUCT;
    if (is_ref) {
        advance(p);
    }
    bool is_unsafe = accept(p, TOK_KW_UNSAFE);
    const Token *t = peek_token(p, 0);
    if (t->kind == TOK_KW_STRUCT) {
        advance(p);
        item->kind = ITEM_STRUCT;
        item->as.struct_.is_copy = is_copy;
        item->as.struct_.is_ref = is_ref;
        parse_struct(p, item);
    } else if (t->kind == TOK_KW_ENUM) {
        advance(p);
        item->kind = ITEM_ENUM;
        item->as.enum_.is_copy = is_copy;
        parse_enum(p, item);
    } else if (t->kind == TOK_KW_FN) {
        advance(p);
        item->kind = ITEM_FN;
        item->as.fn = parse_fn(p, header, true);
        item->as.fn->is_unsafe = is_unsafe;
    } else if (t->kind == TOK_KW_IMPORT) {
        advance(p);
        item->kind = ITEM_IMPORT;
        import(p, item);
    } else if (t->kind == TOK_KW_CONST || t->kind == TOK_KW_LET) {
        advance(p);
        item->kind = t->kind == TOK_KW_CONST ? ITEM_CONST : ITEM_GLOBAL;
        value_item(p, item);
    } else if (t->kind == TOK_KW_EXTERN) {
        advance(p);
        item->kind = ITEM_EXTERN;
        extern_item(p, item, header);
    } else if (at_word(p, p->words.type) && peek_at(p, 1) == TOK_IDENT) {
        advance(p);
        item->kind = ITEM_TYPE_ALIAS;
        type_alias(p, item);
    } else if (t->kind == TOK_KW_INTERFACE) {
        syntax_error(p, t->span, "interfaces are not supported by stage 0");
    } else {
        unexpected(p, "a declaration (`fn`, `struct`, `enum`, `const`, `import`, ...)");
    }
}

static Item *item(Parser *p) {
    ItemHeader header = parse_item_header(p);
    Item *it = NEW(p, Item);
    it->attributes = header.attributes;
    it->is_pub = header.is_pub;
    item_body(p, it, header);
    if (p->recovering) {
        return NULL;   // an incomplete item is left out of the tree
    }
    if (it->kind == ITEM_FN && it->as.fn) {
        it->name = it->as.fn->name;
    }
    it->span = span_since(p, header.span);
    return it;
}

static void module_items(Parser *p, Module *module) {
    Vec(Item *) items = {0};
    skip_newlines(p);
    while (!at(p, TOK_EOF)) {
        Item *it = item(p);
        if (it) {
            VEC_PUSH(items, it);
            expect_terminator(p);
        }
        if (p->recovering) {
            synchronize(p);
            accept(p, TOK_RBRACE);   // a stray `}` at the top level
            skip_newlines(p);
        }
    }
    TO_LIST(p, items, module->items);
}

// Doc comments are not part of the grammar yet (spec 12.5): `///` tokens are
// dropped, `//!` tokens become the module documentation. Compacts `tokens`
// in place rather than copying them.
static void filter_docs(Parser *p, TokenList *tokens, Module *module) {
    Vec(StrView) docs = {0};
    size_t kept = 0;
    for (size_t i = 0; i < tokens->len; i++) {
        Token t = tokens->data[i];
        if (t.kind == TOK_MODULE_DOC) {
            VEC_PUSH(docs, t.as.text);
        } else if (t.kind != TOK_DOC_COMMENT) {
            tokens->data[kept++] = t;
        }
    }
    tokens->len = kept;
    TO_LIST(p, docs, module->docs);
}

Module *parse(Session *session, FileId file, TokenList *tokens) {
    Parser p = {.session = session, .file = file};
    p.words.move = intern(&session->names, SV("move"));
    p.words.type = intern(&session->names, SV("type"));
    p.words.ref = intern(&session->names, SV("ref"));
    p.words.where = intern(&session->names, SV("where"));
    p.words.wildcard = intern(&session->names, SV("_"));

    Module *module = NEW(&p, Module);
    module->file = file;
    filter_docs(&p, tokens, module);
    p.tokens = tokens->data;
    p.count = tokens->len;
    module_items(&p, module);
    return module;
}
