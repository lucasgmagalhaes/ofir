# Standard library reference

This reference lists the public API of the Ofir standard library. It is
normative: the compilers and `std/` implement exactly these signatures.

Entries marked **S0** must be provided by stage 0 (see `docs/bootstrap.md`);
in stage 0 several of them are built into the compiler instead of being
written in Ofir. Entries marked **[planned]** are designed but not
required before version 1.0.

| Module | Reference |
|--------|-----------|
| prelude, `std.core` | [core.md](core.md): `Option`, `Result`, `Ordering`, interfaces, numbers |
| `std.collections`, `std.arena` | [collections.md](collections.md): `List`, `Map`, `Box`, `Arena` |
| `std.strings` | [strings.md](strings.md): `str` methods and helpers |
| `std.io`, `std.fs`, `std.path`, `std.os`, `std.time`, `std.task`, `std.sync` | [system.md](system.md): input/output, files, paths, processes, clocks, dates, tasks, channels |
| `std.mem`, `std.math`, `std.random`, `std.testing`, `std.fmt` | [utilities.md](utilities.md) |
| `std.encoding`, `std.checksum`, `std.json`, `std.compress` | [codecs.md](codecs.md): hex, base64, byte orders, CRC-32, Adler-32, JSON, inflate |
| `std.cli` | [cli.md](cli.md): arguments, help, completions, exit codes, color |

## Planned API ecosystem

The [API ecosystem plan](../design/api-ecosystem.md) tracks proposed I/O,
filesystem, time, networking, codecs and HTTP facilities alongside framework,
database and observability packages. It distinguishes implemented modules
from pending work; proposed names there are not normative public signatures.

## Conventions

- Methods that can fail return `Result` or `Option`; panicking variants
  are named explicitly (`unwrap`, `expect`) or documented with **panics**.
- Methods that allocate say so.
- `len()` and indices are `int` (spec 2.1).
- Methods are listed with their `self` parameter, as declared inside the
  type (spec 3.6); functions without `self` are associated functions, called
  as `List.new()`.
- Parameters are borrowed unless marked `mut` or `move` (spec 5.3); methods
  that store or consume an argument take it with `move`.
- Errors from the system are values of `IoError` (see [system.md](system.md)).

## API contracts

Every public item of the standard library documents its contract in its
doc comment ([ADR 0062](../adr/0062-library-api-contracts.md)); `ofir doc`
renders each tag as a section of its own.

- **Ownership** is in the signature: a parameter is borrowed, `mut` or
  `move`, and a returned reference borrows from `self` or the parameters.
- **Panics** are for preconditions the caller breaks, such as an index out
  of range; `@panics` names each one. A failure of the environment, such as
  a missing file, is a `Result` or an `Option`, never a panic.
- **Complexity**: every method of `List`, `Map` and `std.collections`
  states its time with `@complexity`, amortized where it says so.
- **Allocation**: `@allocates` says when a call allocates where a reader
  would not expect it.
- **Platform**: `@platform` names a behavior that differs between targets.
- **Examples**: every module outside `std.core` opens with an `@example`,
  run as a doc test by `ofir test`.

`tools/check_docs.py` enforces the tags, the complexity of collections and
the module examples. Changes to these contracts follow the versioning
rules of [ADR 0037](../adr/0037-compatibility-policy.md).
