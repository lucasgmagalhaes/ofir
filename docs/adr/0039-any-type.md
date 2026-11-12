# 0039. `any`: a builtin `dyn` type for values of any type

- Status: Accepted
- Date: 2026-11-12

## Context

ADR 0024 added the interface `Any` in `std.core`, used as `dyn Any`:
`v: dyn Any`, `Box<dyn Any>`, `&dyn Any`. `Any` is the only interface
whose sole purpose is to be used dynamically: no generic code bounds a
type parameter by it, and every type satisfies it. Writing `dyn` in front
of it every time is noise, and the capitalized name reads like a user
type rather than a part of the language, next to `int` and `str`.

## Decision

The interface is renamed `any` and becomes a builtin `dyn` type: the name
`any` alone, in any type position, means what `dyn Any` meant.

- `v: any` borrows a value of any type, `&any` and `*any` point at one,
  and `Box<any>` owns one, as `dyn I` does for other interfaces (spec
  9.7).
- `dyn any` is an error (E0365): `any` is dynamic already.
- `typeName`, type tests and the rules on types that hold references are
  unchanged (spec 9.8).
- `any` stays declared in `std/core/any.of`, in the prelude, so the
  compiler keeps one definition of `typeName`. A value or method named
  `any` (`xs.any(...)`, `let any = false`) is not affected.
- The name `Any` is gone; there is no alias.

This supersedes the spelling `dyn Any` of ADR 0024; the rest of that
record stands.

## Consequences

- Programs write `fn describe(v: any) str` and `List<Box<any>>`.
- An interface named as a parameter type stays an anonymous generic
  (spec 9.4), except `any`, which is a `dyn` parameter.
- Code that used `dyn Any` must be rewritten to `any`; before 1.0 there
  is no stability promise (ADR 0037).
