# Appendix A. Grammar

This is the complete syntactic grammar of Ofir in EBNF. Lexical tokens
(identifiers, literals, keywords, operators) are defined in chapter 1.

`NL` is the newline token emitted by the lexer at the end of each line,
except when the innermost open bracket is `(` or `[`. Following chapter 1.3, the parser accepts `NL`
only where it appears in this grammar and silently skips it everywhere else.
Where the grammar allows `NL`, any number of consecutive newlines may appear.
A terminator before `}` may be omitted.

## A.1 Files and items

```ebnf
File          = [ ModuleDoc ] { Import } { Item } EOF .
ModuleDoc     = doc_comment .            (* `/** @module ... */` *)
Import        = "import" ModulePath [ "as" identifier ] NL .
ModulePath    = identifier { "." identifier } .

Item          = [ DocComment ] { Attribute } [ "pub" ] ItemKind .
DocComment    = doc_comment .            (* `/** ... */` *)
ItemKind      = FnDecl
              | StructDecl
              | TupleStructDecl
              | EnumDecl
              | InterfaceDecl
              | ConstDecl
              | GlobalDecl
              | TypeAlias
              | ExternBlock .

Attribute     = "@" identifier [ "(" [ AttrArg { "," AttrArg } ] ")" ] [ NL ] .
AttrArg       = Literal | identifier | AttrCond .
AttrCond      = Expr .                       (* restricted to @cfg conditions *)
```

## A.2 Functions

```ebnf
FnDecl        = [ "unsafe" ] [ "extern" string_lit ] "fn" identifier
                [ TypeParams ] "(" [ Params ] ")" [ Type ]
                [ WhereClause ] Block .

MethodDecl    = { Attribute } [ "pub" ] [ "unsafe" ] "fn" identifier
                [ TypeParams ] "(" [ MethodParams ] ")" [ Type ]
                [ WhereClause ] Block .
MethodParams  = SelfParam [ "," [ Params ] ]       (* method *)
              | Params .                           (* associated function *)
SelfParam     = [ Mode ] "self" .

Params        = Param { "," Param } [ "," ] .
Param         = identifier ":" [ Mode ] Type [ "=" Expr ] .   (* default value *)
Mode          = "mut" | "move" .                   (* chapter 5.3; default: borrow *)

TypeParams    = "<" TypeParam { "," TypeParam } [ "," ] ">" .
TypeParam     = identifier [ ":" Bounds ] .
Bounds        = TypeRef { "+" TypeRef } .
TypeRef       = TypePath [ TypeArgs ] .
WhereClause   = "where" WherePred { "," WherePred } [ "," ] .
WherePred     = Type ":" Bounds .
```

## A.3 Types declarations

```ebnf
StructDecl    = [ "copy" | "ref" ] "struct" identifier [ TypeParams ]
                [ ":" Bounds ] "{" { FieldDecl } { MethodDecl } "}" .
FieldDecl     = { Attribute } [ "pub" ] identifier ":" Type [ "=" Expr ] ( "," | NL ) .
TupleStructDecl = [ "copy" ] "struct" identifier [ TypeParams ]
                "(" [ { Attribute } [ "pub" ] Type { "," { Attribute } [ "pub" ] Type } [ "," ] ] ")"
                [ "{" { MethodDecl } "}" ] .

EnumDecl      = [ "copy" ] "enum" identifier [ TypeParams ]
                [ ":" Bounds ] "{" { Variant } { MethodDecl } "}" .
Variant       = identifier [ "(" VariantField { "," VariantField } [ "," ] ")" ]
                [ "=" int_lit ] ( "," | NL ) .
VariantField  = [ identifier ":" ] Type .

InterfaceDecl = "interface" identifier [ TypeParams ] "{" { IfaceMember } "}" .
IfaceMember   = TypeRef NL                                 (* embedding *)
              | "fn" identifier [ TypeParams ]
                "(" [ MethodParams ] ")" [ Type ] [ WhereClause ]
                ( Block | NL ) .                           (* default body optional *)

ConstDecl     = "const" identifier [ ":" Type ] "=" Expr NL .
GlobalDecl    = "let" identifier ":" Type "=" Expr NL .
TypeAlias     = "type" identifier [ TypeParams ] "=" Type NL .

ExternBlock   = "extern" string_lit "{" { { Attribute } FnSignature NL } "}" .
FnSignature   = "fn" identifier "(" [ Params ] ")" [ Type ] .
```

## A.4 Types

```ebnf
Type          = TypePath [ TypeArgs ]
              | "Self"
              | "&" [ "mut" ] Type
              | "*" [ "mut" ] Type
              | "[" Type [ ";" Expr ] "]"       (* slice element or fixed array *)
              | "(" [ Type { "," Type } [ "," ] ] ")"   (* unit or tuple *)
              | [ "move" | "extern" string_lit ] "fn" "(" [ ParamType { "," ParamType } ] ")" [ Type ]
              | "dyn" Bounds
              | Type "?"                        (* Option<Type> *)
              | "!" .

TypePath      = identifier { "." identifier } .
ParamType     = [ Mode ] Type .                     (* `fn(mut List<int>)` *)
TypeArgs      = "<" Type { "," Type } [ "," ] ">" .
```

A one-element tuple type is written `(T,)`; `(T)` is the type `T`. `[T]` without a length is only
valid behind a reference (`&[T]`, `&mut [T]`).

Type operators (chapter 16): `keyof` and `infer` are
contextual keywords in types.

```ebnf
Type          = ...
              | "keyof" Type                    (* the key type of a struct or tuple *)
              | KeyLiteral
              | Type "[" Type "]"               (* indexed access: Point[.x] *)
              | Type ( "|" | "&" | "-" ) Type   (* operations on key types *)
              | MappedType
              | "if" Type "is" Type "{" Type "}" "else" "{" Type "}"
              | "infer" identifier .            (* only in the pattern after `is` *)

KeyLiteral    = "." ( identifier | int_lit ) .
MappedType    = "{" "[" identifier "in" Type [ "if" Type ] "]" [ "?" ] ":" Type "}" .
```

Union types (chapter 17): `A | B` between types that are not
key types. `|` binds looser than `?`, so `A? | B` is `Option<A> | B`.

```ebnf
Type          = ...
              | Type "|" Type .                 (* union type *)
```

## A.5 Statements

```ebnf
Block         = "{" { Statement } "}" .
Statement     = ( ConstStmt
                | LetStmt
                | ConstDecl
                | AssignStmt
                | [ identifier ":" ] ForStmt
                | ReturnStmt
                | "break" [ identifier ]
                | "continue" [ identifier ]
                | ExprStmt ) [ NL ] .

ConstStmt     = "const" Pattern [ ":" Type ] "=" Expr [ "else" Block ] .
LetStmt       = "let" identifier [ ":" Type ] [ "=" Expr ] .
AssignStmt    = Expr AssignOp Expr .
AssignOp      = "=" | "+=" | "-=" | "*=" | "/=" | "%="
              | "&=" | "|=" | "^=" | "<<=" | ">>=" .
ForStmt       = "for" [ ForHeader ] Block .
ForHeader     = Expr                                       (* condition *)
              | [ "mut" ] identifier [ "," identifier ] "in" Expr .  (* iteration *)
ReturnStmt    = "return" [ Expr ] .
ExprStmt      = Expr .
```

## A.6 Expressions

```ebnf
Expr          = RangeExpr .
RangeExpr     = CoalesceExpr [ ( ".." | "..=" ) CoalesceExpr ] .
CoalesceExpr  = OrExpr [ "??" CoalesceExpr ] .               (* right-associative *)
OrExpr        = AndExpr { "||" AndExpr } .
AndExpr       = CmpExpr { "&&" CmpExpr } .
CmpExpr       = AddExpr [ CmpOp AddExpr ] .              (* non-associative *)
CmpOp         = "==" | "!=" | "<" | "<=" | ">" | ">=" .
AddExpr       = MulExpr { ( "+" | "-" | "|" | "^" ) MulExpr } .
MulExpr       = CastExpr { ( "*" | "/" | "%" | "<<" | ">>" | "&" ) CastExpr } .
CastExpr      = UnaryExpr { "as" Type } .
UnaryExpr     = ( "-" | "!" | "*" | "&" [ "mut" ] ) UnaryExpr
              | PostfixExpr .

PostfixExpr   = Primary { Postfix } .
Postfix       = "(" [ Args ] ")"                 (* call *)
              | "[" Expr "]"                     (* index or slice *)
              | "." identifier                   (* field or method *)
              | "." int_lit                      (* tuple field *)
              | "?" .                            (* error propagation *)
Args          = Arg { "," Arg } [ "," ] .
Arg           = [ identifier ":" ] Expr .                   (* named argument *)

Primary       = Literal
              | identifier
              | "Self"
              | TypePath TypeArgs                (* see 8.5 for disambiguation *)
              | "(" Expr ")"
              | "(" [ Expr "," [ Expr { "," Expr } [ "," ] ] ] ")"   (* tuple *)
              | ArrayLit
              | StructLit
              | IfExpr
              | MatchExpr
              | Closure
              | Block
              | "unsafe" Block .

ArrayLit      = "[" [ Expr { "," Expr } [ "," ] ] "]"
              | "[" Expr ";" Expr "]" .
StructLit     = TypePath [ TypeArgs ] "{" [ FieldInit { Sep FieldInit } ]
                [ Sep ".." Expr ] [ Sep ] "}" .
FieldInit     = identifier [ ":" Expr ] .          (* `x` is short for `x: x` *)
Sep           = "," | NL .

IfExpr        = "if" Cond Block [ "else" ( IfExpr | Block ) ] .
Cond          = Expr | "const" Pattern "=" Expr .
MatchExpr     = "match" Expr "{" { MatchArm } "}" .
MatchArm      = Pattern [ "if" Expr ] "=>" ( Expr | Block ) ( "," | NL ) .

Closure       = [ "move" ] ClosureHead "=>" ( Expr | Block ) .   (* chapter 10.1 *)
ClosureHead   = identifier
              | "(" [ ClosureParam { "," ClosureParam } [ "," ] ] ")" [ Type ] .
ClosureParam  = identifier [ ":" [ Mode ] Type ] .

Literal       = int_lit | float_lit | string_lit | raw_string_lit
              | byte_lit | byte_string_lit
              | "true" | "false" .
```

**Struct literal restriction.** In the condition of `if`, `match` and the
header of `for`, a struct literal must be parenthesized, as in Go:
`if p == (Point{x: 0, y: 0}) { }`. Otherwise the `{` is the start of the
block.

## A.7 Patterns

```ebnf
Pattern       = PatternAtom { "|" PatternAtom } .
PatternAtom   = "_"
              | [ "mut" ] identifier                    (* binding *)
              | Literal
              | [ "-" ] Literal ( ".." | "..=" ) [ "-" ] Literal
              | TypePath "." identifier [ "(" PatternList ")" ]
              | TypePath "{" FieldPat { "," FieldPat } [ "," ".." ] "}"
              | TypePath "(" PatternList ")"                (* tuple struct *)
              | "(" PatternList ")"                     (* tuple *)
              | ( identifier | "_" ) ":" Type .         (* type test: dyn or union *)
PatternList   = Pattern { "," Pattern } [ "," ] .
FieldPat      = identifier [ ":" Pattern ] .
```

## A.8 Rules not expressed by the grammar

- `let x` must have a type, a value, or both (`let x: int`, `let x = 0`).
- A closure is recognized by bounded lookahead (chapter 10.1): an
  identifier followed by `=>`, or `( ... )` followed by `=>` or by a type
  and `=>`. In a `match` guard, a closure is written in parentheses.
- A `const` pattern must be irrefutable (it cannot fail to match).
- `Variant = int_lit` (explicit discriminant) is allowed only in enums with
  no payloads; values must be distinct. With `@repr(C)`, they define the C
  enum values.
- Attributes are only valid on the items listed in chapter 12.1.
- In a parameter `f: move fn(A) R`, `move` is read as the parameter mode.
  A parameter whose type is the owned function type is always taken in
  `move` mode, so this is the only spelling and has a single meaning.
- In a type test pattern, a union type is written in parentheses
  (`e: (A | B)`): a bare `|` there separates alternatives of the pattern.
- Contextual keywords (chapter 1.5) are keywords only in the positions
  shown in this grammar.
