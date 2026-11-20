# 0102. Scope and capacity are a checked page

- Status: Proposed
- Date: 2026-11-20

## Context

OFIR-86 (roadmap 6.31i) asks for scope and capacity governance. Its
evidence is priorities, owners, dependencies, estimates per small pull
request, and explicit rejected alternatives.

With OFIR-86, all 86 records of the language completeness plan have a
decision. The roadmap still has open work: 6.24a, the commands of 6.32,
the guide gaps of 6.31c and the API rows 6.11 to 6.21a. The project has
one maintainer (`docs/maintaining.md`), and nothing says which of these
rows comes first, which wait, or how large the next change is. The
roadmap kept a stale row too: 2.12 said `in progress`, but its views,
`&T` locals and `*` had passed in both stages for a long time.

The decisions against an alternative are spread over ADRs 0037 to 0101,
the spec and the completeness plan's non-goals. A new proposal does not
find them in one place.

## Decision

1. **`docs/design/scope.md` holds the governance.** It has the open
   rows with a priority (G0, G1, G2), an owner, dependencies and a
   state (`in progress`, `next`, `queued`). It lists the estimated pull
   requests of the active rows, and the rejected and deferred
   alternatives with their ADR or spec section and the condition to
   reopen each.
2. **Capacity is a limit per maintainer.** A maintainer has at most one
   row `in progress` and two `next`. A row starts only when the rows it
   depends on are done.
3. **Only active rows are estimated.** A pull request is S (about 300
   changed lines or fewer) or M (about 800 or fewer). An L is split
   before it is listed. A queued row gets its estimates when it becomes
   `next`.
4. **A G2 row needs a workload and an owner.** Without them its
   decision goes to the rejected and deferred table, as OFIR-83 to
   OFIR-85 did.
5. **`tools/check_scope.py` keeps the page true** in `make check-docs`.
   It checks that every open roadmap row is listed and no done one is,
   that owners are maintainers, that dependencies form no cycle and
   active rows have none open, the capacity limit, the sizes, and that
   every decision links a file that exists.

Rejected: estimating every open row now, because far estimates are
guesses. Also rejected: tracking the work in an issue tracker, which
the checks cannot read and which pull requests do not review.

## Consequences

- A change to the order of work is a pull request to `scope.md`, which
  the checks review together with the roadmap.
- When a second maintainer joins, the capacity doubles, and rows can
  take their name as owner.
- Roadmap rows are kept accurate. A row that stays `in progress` after
  it is done fails the check only when it is missing from `scope.md`,
  so the open list is reviewed whenever a row starts.
