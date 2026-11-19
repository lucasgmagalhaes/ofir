# Representative applications

Real programs that must keep building and passing their cases across
releases (OFIR-76, [ADR 0090](../docs/adr/0090-representative-applications.md)).
Each is written as a user would write it, against the public std.

| App | Kind | Uses |
| --- | --- | --- |
| [seek](seek) | CLI and filesystem utility: searches a directory tree for a text | std.cli, std.fs, std.path, std.os |
| [textstats](textstats) | C consumer: an Ofir static library linked by a C program | `--lib=static`, `@repr(C)`, the generated header, std.checksum |
| [wordfreq](wordfreq) | Concurrent program: one task per file, counts merged from a channel | std.task, std.sync (channel, `Arc`, `AtomicInt`), std.fs |

The compiler (`compiler/`) is the largest application; `make verify` and
the self-compile budgets check it. The API service joins when std has
an HTTP server (API-49).

Each `app.json` names the root source, the library kind and C consumer
if any, and the cases: arguments, exit status and the expected standard
output in `expected/<case>.out`. `make check-apps` builds every app with
stage 1 in dev and with `--release` and runs every case against both:

```
make check-apps
python3 tools/check_apps.py --compiler build/stage1/ofir seek
```
