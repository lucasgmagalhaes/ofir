# 0058. Paths as strings, a wall clock, UTC dates and more file operations

- Status: Accepted
- Date: 2026-11-14

## Context

OFIR-43 asks for streams, the file system, paths, processes, the
environment, clocks and dates, with portable contracts and cleanup tests.
The system modules of API-01 to API-08 already cover part of it:

- `std.io` reads standard input and flushes standard output.
- `std.fs` reads, writes, renames and removes files, creates directories
  and lists them.
- `std.os` has arguments, the environment, `spawn` and `run`.
- `std.time` has a monotonic clock, `sleep` and `Deadline`.

Three things are missing:

- A way to take paths apart and put them together without string
  slicing in every program.
- The time of day: `time.now()` is monotonic and has no epoch.
- File operations that programs often write by hand: appending, copying
  and removing a directory.

## Decision

1. **`std.path` works on strings.**
   - A path is a `str`, as everywhere in `std.fs`. There is no `Path`
     type, since a separate type would need a conversion at every call.
   - `/` separates components on every system. On Windows `\` does too,
     and a drive (`C:`) starts an absolute path, so paths that come from
     the system work there.
   - The functions are pure: they never touch the file system.
     - Parts: `parent`, `fileName`, `stem` and `extension`.
     - Tests: `isAbsolute` and `hasExtension`.
     - Building: `join`, `withExtension` and `components`.
     - Lexical cleanup: `normalize` removes `.`, empty components and the
       `..` that follow a name. It does not resolve symbolic links;
       `fs.canonicalize` does.
   - Results that are part of the argument borrow it. Results that are
     new strings are owned.
2. **`time.unixNanos()` reads the wall clock.**
   - It returns nanoseconds since 1970-01-01T00:00:00Z, from
     `CLOCK_REALTIME` (on Windows, the system time as a file time).
   - It can jump when the system clock is set. Durations still use
     `time.now()`, which never goes back.
3. **`std.time.date` has UTC calendar dates.**
   - `DateTime` holds the year, month, day, hour, minute, second and
     nanosecond, in UTC. Time zones and local time stay out: they need the
     system's time zone database.
   - Converting to and from Unix time uses Howard Hinnant's
     days-from-civil algorithm, which is exact for any year a 64-bit
     count of days can hold. The proleptic Gregorian calendar applies to
     every year.
   - `format` writes RFC 3339 (`2026-11-14T08:05:09Z`, with a fraction
     only when the nanoseconds are not 0). `parse` reads RFC 3339 with
     `Z` or a numeric offset, and converts the offset to UTC.
   - The helpers are `isLeapYear`, `daysInMonth` and `weekday` (0 is
     Monday, as in ISO 8601).
   - Leap seconds are not represented, as in Unix time. `parse` rejects a
     second of 60.
4. **`std.fs` gains `append`, `appendString`, `copy` and `removeDir`.**
   - `append` opens the file with `O_APPEND`, creating it if it is
     missing.
   - `copy` reads the source and writes the destination, which is
     replaced if it exists. It copies the contents only, not the
     permissions or times.
   - `removeDir` removes an empty directory and fails on any other.
     Recursive removal stays out of this change.

## Consequences

- `std.path` costs nothing at run time beyond string slicing, and its
  results are the same on every system for paths that use `/`.
- Programs can print and store timestamps without writing calendar
  arithmetic, and a timestamp written on one system parses on another.
- Local time, time zones, durations as a type and streaming `Reader`
  and `Writer` interfaces remain open. The interfaces wait for API-10.
