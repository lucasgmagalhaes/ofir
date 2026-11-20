# 0094. Service ecosystem records

- Status: Proposed
- Date: 2026-11-20

## Context

OFIR-79 asks for the API and service ecosystem (roadmap 6.11 to 6.21a)
by reusing the 54 records of the
[API ecosystem plan](../design/api-ecosystem.md) and their gates, not
by planning the work a second time.

The plan was written as a survey on 2026-11-04 and said that every
record was pending. Since then `std` gained `std.time`, `std.path`,
`std.json`, `std.encoding`, `std.compress`, `std.checksum`,
`std.random`, `std.sync` and `std.task`, and 6.29 gave packages a
lock, publishing, an SBOM and advisories. The plan did not say which
records these cover, so it could not tell how far a gate was. Its gates
were prose lists that nothing checked: a record of the local gate could
depend on one of the production gate, a record could be in no gate,
and a dependency could name a record that does not exist.

Each record is a separate deliverable with its own tests (sockets, TLS,
HTTP, databases). OFIR-79 cannot deliver them in one change, and it
must not claim them.

## Decision

1. **The 54 API records are the backlog of OFIR-79.** No OFIR record
   repeats one; work on an API record links its API ID. OFIR-79 is the
   maintained plan and its check, not the implementation of the
   records.
2. **Each record has a state.** The plan's `## Record states` table
   gives each record, in order, a state and its evidence:
   - `pending`: nothing in the repository provides it yet;
   - `partial`: a module or tool covers part of it; the row cites
     existing files and says what is missing;
   - `done`: its completion criterion holds; the row cites at least one
     test, and nothing is missing.
   A design, a prototype or one passing request does not make a record
   `done`.
3. **The gates are a table.** `## Delivery gates` lists, for API-F,
   API-L, API-P and API-X, the gate it builds on and the records it
   adds. Every record is in exactly one gate, except a record whose
   priority names two variants (API-49, local and production). A gate is
   `reached` only when it and the gate it builds on have every record
   `done`; the table says `open` until then. API-X has no state: its
   records are accepted one by one.
4. **The dependencies are checked.** A dependency that is only an API ID
   is a hard one; one with a condition (`API-11 for HTTPS`, `API-38 when
   used`) is not. A hard dependency must name an existing record other
   than itself, and must be in the same gate as its record or an
   earlier one. The hard dependencies have no cycle.
5. **Priorities match the gates.** A P0 record is in API-F or API-L, a
   P2 record is in API-X.
6. **`tools/check_api_plan.py` checks all of this** in `make check-docs`,
   with the record ranges of each roadmap section (6.11 to 6.21a)
   against the plan's headings.

## Consequences

- The state of the service ecosystem can be read in one table, and a
  record cannot be marked `done` without a test that exists.
- A pull request that adds a module for an API record updates its row;
  one that removes the evidence of a row fails the check.
- The gates stay open. The first gate, API-F, needs the stream,
  filesystem, error, entropy and socket contracts; the decisions listed
  in the plan (module ownership, the TLS and database providers) still
  come first, each with its own ADR.
- The reference service of `apps/` (ADR 0090) waits for API-49 as
  before.
