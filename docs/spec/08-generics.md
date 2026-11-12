# 8. Generics

Generics let functions and types work over many types without losing static
typing or performance. Ofir implements them by **monomorphization**: each
distinct instantiation is compiled to specialized code, so generic code is
as fast as hand-written code.

## 8.1 Generic functions

```ofir
fn max<T: Ord>(a: move T, b: move T) T {
    if a > b {
        return a
    }
    return b
}

const m = max(3, 7)           // T inferred as int
const s = max("a", "b")       // T inferred as str
```

```ebnf
TypeParams = "<" TypeParam { "," TypeParam } [ "," ] ">" .
TypeParam  = identifier [ ":" Bounds ] .
Bounds     = TypeRef { "+" TypeRef } .
```

A type parameter can only be used in ways allowed by its bounds. Without
bounds, a value of type `T` can only be moved, dropped, borrowed and passed
to other generic code.

Until interfaces (chapter 9, roadmap 5.3), a bound names one of the
built-in interfaces:

| Bound | Allows | Satisfied by |
|-------|--------|--------------|
| `Eq` | `==`, `!=` | integers, floats, `bool`, `str`, `()`, pointers, enums without payloads, tuples of these |
| `Ord` | `<`, `<=`, `>`, `>=`, `list.sort()` | integers, floats (IEEE, chapter 2.1), `str` |
| `Hash` | with `Eq`, `Map<T, V>` keys | integers, `bool`, `str`, tuples of these |
| `Clone` | `.clone()` | every type that can be cloned (chapter 5.2) |
| `Copy` | implicit copies of values of `T` | Copy types (chapter 2.9) |

A type parameter satisfies a bound it declares. A bound that names no
interface is an error (E0333), and so is a bound on a type that is not a
type parameter of the declaration (E0334). Type arguments, written or
inferred, of calls, struct literals, variants and types such as
`Tree<Point, int>` must satisfy the bounds of their parameters (E0332).

Stage 0 does not accept bounds; stage 1 does.

## 8.2 Generic types

```ofir
struct Stack<T> {
    items: List<T>
}

enum Tree<K: Ord, V> {
    Leaf
    Node(key: K, value: V, left: Box<Tree<K, V>>, right: Box<Tree<K, V>>)
}
```

## 8.3 Methods on generic types

The type parameters of a generic type are in scope in all of its methods:

```ofir
struct Stack<T> {
    items: List<T>

    fn new() Stack<T> {
        return Stack{items: []}
    }

    fn push(mut self, item: move T) {
        self.items.push(item)
    }

    fn pop(mut self) Option<T> {
        return self.items.pop()
    }
}
```

Methods may add their own type parameters and bounds:

```ofir
fn contains(self, item: T) bool where T: Eq { ... }
fn map<U>(self, f: fn(T) U) Stack<U> { ... }
```

## 8.4 `where` clauses

Bounds may be written after the signature when they are long. The
clause comes after the result type, on the same line or the next ones; the
bounded type is one of the declaration's type parameters, including those
of the type of a method (`where T: Eq` in a method of `Stack<T>` binds only
that method):

```ofir
fn merge<K, V>(a: move Map<K, V>, b: move Map<K, V>) Map<K, V>
where
    K: Eq + Hash,
    V: Clone,
{
    ...
}
```

## 8.5 Explicit type arguments

Type arguments are normally inferred from arguments and from the expected
type. In order, they come from what is written, the receiver (`s.push(x)`
on a `Stack<int>`), the expected type, then the value arguments from left
to right; the first type found for a parameter is its argument, and the
arguments are then checked against the substituted signature. A parameter
that none of them determines is an error (E0330). They can be written
explicitly:

```ofir
const xs = List<int>.new()
const n = parse<u16>("8080")?
const size = sizeOf<Point>()
```

**Disambiguation.** Because comparison operators do not chain (chapter
4.8), `a < b > c` is never a valid comparison. In expressions, `Name<...>`
is parsed as type arguments when the tokens between `<` and `>` form a valid
type list (integer literals count as const arguments, chapter 8.7) **and** the `>` is immediately followed by `(`, `.` or `{`.
Otherwise `<` is the less-than operator. In the rare case where an argument
list really contains `a < b, c > (d)`, parenthesize the comparison.

## 8.6 Instantiation rules

- Every instantiation must be resolvable at compile time; there is no
  runtime type information except for `dyn` (chapter 9).
- Recursive instantiation that would generate infinitely many types
  (`fn f<T>() { f<List<T>>() }`) is a compile error, detected by a fixed
  depth limit (64).
- Bounds are checked at the definition, not at each instantiation: a generic
  function that compiles is valid for every type satisfying its bounds.
  This gives clear errors, unlike C++ templates.

## 8.7 Const generics

A type parameter written `const N: int` is a **const parameter**: its
argument is a compile-time integer rather than a type. Inside the
declaration, `N` is a value of type `int` and may be the length of an
array type.

```ofir
struct Buffer<T, const N: int> {
    items: [T; N]

    fn capacity(self) int {
        return N
    }
}

fn sum<const N: int>(xs: [int; N]) int {
    let total = 0
    for x in xs {
        total += x
    }
    return total
}

fn main() {
    const b = Buffer<u8, 4>{items: [0; 4]}
    println("{b.capacity()} {sum([1, 2, 3])}")   // 4 6
}
```

- A const argument is an integer literal (optionally negated), a `const`
  item of type `int`, or a const parameter in scope. It is inferred like a
  type argument (chapter 8.5): `sum([1, 2, 3])` infers `N = 3` from the
  array's length.
- The type of a const parameter is `int`; other types are an error
  (E0361). A type where a constant is expected, or a constant where a type
  is expected, is an error (E0362).
- Each distinct argument is a separate instantiation: `Buffer<u8, 4>` and
  `Buffer<u8, 8>` are different types.
- The length of an array type in a generic declaration is an integer
  constant or a const parameter. Arithmetic on const parameters in types
  (`[float; R * C]`) is **[planned]**.
