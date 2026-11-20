# API and service ecosystem plan

This is the service-specific subplan of the
[language completeness inventory](language-completeness.md). Shared language,
runtime, library and tooling prerequisites are tracked there without duplicating
implementation tasks. General-purpose readiness also requires non-service workloads.

Status: **proposed backlog**, initial survey against `4b1da75` on 2026-11-04;
API-50–54 added as operational follow-ups on 2026-11-19; starting point and
record states reconciled on 2026-11-20 (OFIR-79, ADR 0094).
This is an implementation plan, not a normative standard-library API.
Module/package names below describe intended homes; none is promised as an
existing import. The state of each record is in [Record states](#record-states),
checked by `tools/check_api_plan.py`; no record is done yet.

The goal is to build, test and operate HTTP/REST services in Ofir: accept
requests, validate data, access storage, call other services, and diagnose
failures. The survey covers language/runtime prerequisites, portable library
facilities and ecosystem packages. Protocols and database vendors beyond
the initial service stack are explicit extensions, not universal requirements.

## Verified starting point

| Area | Evidence in this revision | Gap for API services |
|------|---------------------------|----------------------|
| Ownership and errors | [Ownership](../spec/05-ownership.md), [errors](../spec/06-errors.md), `Option`/`Result` in `std/core` | Shared-resource and cancellation conventions; causal error chains |
| Collections and text | [std/core](../../std/core/index.of), [std.strings](../../std/strings/index.of) | Public codecs, Unicode validation/normalization and bounded streaming buffers |
| I/O | [std.io](../../std/io/index.of): `IoError`, stdin, runtime buffers | General readers/writers, seeking, buffered streams and short-read/write contracts |
| Filesystem | [std.fs](../../std/fs/index.of): whole-file read/write/append, copy, rename, metadata, create/remove, directory listing; [std.path](../../std/path/index.of) | Owned file handles, seeking, permissions, temporary files, atomic replacement and large-file streaming |
| Processes and configuration | [std.os](../../std/os/index.of): args, env, run, cwd, executable path | Typed configuration, signals, supervised processes and graceful service lifecycle |
| Concurrency | [Chapter 14](../spec/14-concurrency.md), roadmap 6.1: [std.task](../../std/task/index.of) and [std.sync](../../std/sync/index.of) (channels, `Mutex`, `RwLock`, `Arc`, atomics, `WaitGroup`, `CancelToken`), `Send`/`Sync` | Worker pools, overload and cancellation integrated with a service runtime |
| Time | [std.time](../../std/time/index.of): monotonic `now`, `unixNanos`, `sleep`, `Deadline`; leap years and month lengths | No `Duration`/`Instant` types, injectable clock, calendar date/time, RFC 3339 or timezones |
| Networking and data exchange | [std.json](../../std/json/index.of) (value tree, depth limit), [std.encoding](../../std/encoding/index.of) (hex, base64), [std.compress](../../std/compress/index.of) (bounded inflate, gunzip) | Sockets, DNS, TLS, HTTP, URLs, forms, streaming codecs and compression output |
| Persistence and operations | No public database, logging, metrics or tracing modules in `std/` | Drivers, transactions, observability and production examples |
| Package distribution | Roadmap 6.5 and 6.29: lockfile, registry publishing, native inputs, SBOM, advisories (ADRs 0076 to 0079 and 0081 to 0084) | No framework or driver package published yet |

The [system reference](../std/system.md) already describes `File`, readers,
writers and path helpers beyond the implemented subset. Extend those
contracts rather than introduce competing APIs. The presence of `ofir.json`
in module discovery is not a general-purpose JSON codec.

## Placement and prerequisites

These are recommendations for later ADRs; this document does not assign new
syntax or approve a cryptography, TLS, database or async backend.

- **Runtime/compiler (R):** OS handles, clocks, entropy, scheduling integration,
  `Send`/`Sync` and safe ownership across threads. Keep the C boundary small,
  following [ADR 0019](../adr/0019-standard-library-in-ofir.md).
- **Standard-library candidates (S):** I/O, filesystem/paths, time, networking,
  codecs, HTTP primitives and a logging facade. Select their final surface
  before committing to the 1.0 stability guarantee.
- **Packages (P):** router/REST framework, typed validation, TLS/crypto providers,
  database drivers, migrations, ORM, auth integrations and telemetry exporters.
  Their releases and vendor dependencies can evolve independently of Ofir.

Existing roadmap dependencies remain authoritative:

| Existing task | API use | Can initial work proceed before it? |
|---------------|---------|-------------------------------------|
| 5.3 interfaces; 5.4 `dyn` | Stream/driver contracts and optional runtime-selected backends | Concrete adapters can prototype first; `dyn` is only needed for dynamic dispatch |
| 5.7 closures; 5.8 iterators | Handlers, middleware, streaming and row iteration | Explicit functions and state objects can provide the first API |
| 5.11 attributes; 6.7a compile-time code | Derived codecs, route/schema metadata, typed query generation | Manual codecs and explicit route registration must remain usable |
| 5.17-5.19 type operators | Optional schema transformation and DTO derivation | HTTP, REST and SQL must not wait for these features |
| 6.1 threads/channels, `Send`/`Sync`, `Arc` | Concurrent servers, pools and worker supervision | Single-threaded client/server experiments can precede it |
| 6.5 package manager | Publish and consume drivers/frameworks reproducibly | In-tree prototypes first; document temporary build/link steps |
| 6.8 platform support | Socket, TLS, filesystem and process behavior across OSes | Test the new module on each supported target; compiler portability alone is insufficient |

Start with blocking I/O and a **bounded** worker model, consistent with chapter
14. Add deadlines and cancellation to contracts before parallel execution.
Async/event-loop backends require a separate design and evidence from measured
workloads; they are not prerequisites for the first HTTP API.

## Backlog

**P0** = first usable local API; **P1** = production service baseline;
**P2** = optional integrations or later scale. Priorities are sequencing aids,
not estimates or release promises. R/S/P refer to the placement above. Each
row is a separate deliverable with its own tests and small commits.

### Foundations — roadmap 6.11

| ID | Priority / home | Deliverable and completion criterion | Dependencies |
|----|-----------------|--------------------------------------|--------------|
| API-01 | P0 / S | Stream I/O: reader/writer/seek contracts, bytes, buffering, copy helpers and explicit ownership/close. Tests cover short I/O, EOF, interruption, bounded memory and error propagation. | 5.3 for generic contracts |
| API-02 | P0 / R+S | Extend filesystem and paths: handles, open modes, append/seek, metadata, rename/copy, temporary files, permissions and atomic replacement. Distinguish flush from durable sync; test large files, symlinks, traversal containment, Unicode paths and cleanup across OSes. | API-01 |
| API-03 | P0 / R+S | `Duration`, monotonic `Instant`, wall-clock timestamp, checked conversions, sleep/timer primitives and injectable clocks. Timeout tests survive wall-clock jumps. | OS clock boundary |
| API-04 | P1 / S+P | Calendar dates, times, UTC offsets, RFC 3339 parsing/formatting, timezone database adapter and explicit ambiguous/nonexistent local-time handling. Leap-year, DST and precision tests pass; document leap-second policy. | API-03 |
| API-05 | P1 / S+P | Data primitives: checked UTF-8 and Unicode policy, exact decimal/rounding for money, UUID parse/format and v4/v7 generation. Binary, text and database round trips preserve precision; clock regression is tested for time-based IDs. | API-03, API-09 |
| API-06 | P0 / S | Error conventions: stable categories, source/cause chains, contextual wrapping, retry classification and redacted display. Expected I/O failures remain `Result` values and retain their causes across HTTP/SQL layers. | Existing `Result` |
| API-07 | P0 / P | Typed configuration with defaults, env/file/CLI precedence, startup validation and injectable secret sources. `.env` is a development option; diagnostics and dumps redact secrets. Config tests need no real credentials. | API-02, API-06; API-13 for JSON config |
| API-08 | P0 / R+S | Request scope, deadline/cancellation token, signals, resource startup/teardown and graceful drain. An interrupted request closes streams and releases leases; shutdown has a deadline. Distinguish task cancellation from forcibly killing a thread. | API-01, API-03, API-06; 6.1 for cross-thread propagation |

### Networking and security primitives — roadmap 6.12

| ID | Priority / home | Deliverable and completion criterion | Dependencies |
|----|-----------------|--------------------------------------|--------------|
| API-09 | P0 / R+S+P | OS cryptographic entropy, explicit deterministic test RNG, hashes/HMAC and constant-time comparison through reviewed providers. Hashing passwords/KDFs is a separate provider capability. Entropy failure must return an error. | API-06; provider ADR |
| API-10 | P0 / R+S | IP addresses, IPv4/IPv6, DNS, TCP listener/stream, UDP and local-socket capability where supported. Connect/read/write deadlines, partial operations and half-close have tests; no unbounded resolver or accept queue. | API-01, API-03, API-08 |
| API-11 | P1 / P | TLS client/server adapter with certificate-chain and hostname validation, trust roots, SNI, ALPN, certificate reload and optional mTLS. Invalid certificates fail closed; backend/version/platform policy is documented. | API-09, API-10 |
| API-12 | P0 / R+S | Integrate bounded worker pools, channels, synchronization and shared handles with the service runtime. Race, overload, cancellation and resource-release tests pass. Optional nonblocking/async backend is P2. | 6.1; API-08, API-10 |

### Serialization and payloads — roadmap 6.13

| ID | Priority / home | Deliverable and completion criterion | Dependencies |
|----|-----------------|--------------------------------------|--------------|
| API-13 | P0 / S | JSON value tree plus streaming parser/writer, UTF-8 validation, depth/size limits and explicit duplicate-key/number-precision policies. Distinguish null from missing; malformed input and round-trip corpus pass. | API-01, API-06 |
| API-14 | P1 / P | Typed encoding/decoding, custom codecs, field names/defaults, unknown-field policy, nested error paths and schema evolution. Manual codecs first; derive helpers later. Optional CBOR/MessagePack/Protobuf/CSV/XML adapters stay independent. | API-13; 5.3; 5.11/6.7a only for derivation |
| API-15 | P0 / S+P | URL/URI, percent encoding, query multimaps, media types, base64/hex, form encoding and streaming multipart. Repeated/empty values and malformed escapes work predictably; uploads have quotas and bounded temporary storage. | API-01, API-02, API-06 |
| API-16 | P1 / P | Streaming gzip/deflate adapters, negotiated compression and explicit compressed/expanded byte limits. Truncation and decompression-limit tests pass; add Brotli/Zstd only as optional providers. | API-01 |

### HTTP — roadmap 6.14

| ID | Priority / home | Deliverable and completion criterion | Dependencies |
|----|-----------------|--------------------------------------|--------------|
| API-17 | P0 / S | HTTP request/response, methods/status, header multimap, body streams and HTTP/1.1 framing. Enforce header/body limits and reject ambiguous message framing; exercise keep-alive, chunking, trailers, HEAD and connection shutdown. | API-01, API-06, API-15 |
| API-18 | P0 / S+P | HTTP client: connection reuse, timeouts, streaming, redirect policy, proxy support and cancellation. HTTPS uses API-11; tests prevent credential forwarding across origins and retrying unreplayable bodies. | API-08, API-10, API-17; API-11 for HTTPS |
| API-19 | P0 / S+P | HTTP server: listen/serve lifecycle, bounded concurrency, header/body/idle timeouts, request limits, keep-alive and graceful shutdown. Slow clients and disconnects release resources. | API-08, API-10, API-12, API-17 |
| API-20 | P2 / P | HTTP/2 adapter: ALPN, streams, flow control, cancellation and connection draining. Interoperability and bounded-memory tests pass before advertising support. | API-11, API-17, API-18, API-19 |
| API-21 | P2 / P | HTTP/3/QUIC adapter with an explicit dependency/update policy and measurable benefit. UDP loss, connection migration and congestion/flow-control tests are provider-backed. | API-10, API-11, API-17; separate ADR |

HTTP semantics and HTTP/1.1 framing are separate contracts:
[RFC 9110](https://www.rfc-editor.org/rfc/rfc9110.html) and
[RFC 9112](https://www.rfc-editor.org/rfc/rfc9112.html). TLS interop targets
[TLS 1.3](https://www.rfc-editor.org/rfc/rfc8446.html), with any additional
supported versions recorded by the provider policy.

### REST/application framework — roadmap 6.15

| ID | Priority / home | Deliverable and completion criterion | Dependencies |
|----|-----------------|--------------------------------------|--------------|
| API-22 | P0 / P | Router with method/path matching, groups, typed path/query extraction, ordered middleware, explicit application state and startup dependencies. Test route conflicts, percent-decoding boundaries, 404/405 and handler errors. | API-15, API-19; 5.7 for closure ergonomics |
| API-23 | P0 / P | Input binding/validation: required vs optional/null, nested fields, numeric/range/length constraints, custom validators and unknown fields. Return structured field errors; malformed or oversized bodies never reach handlers. | API-13, API-22; API-14 for generated codecs |
| API-24 | P0 / P | REST conventions: resource status codes, Problem Details, content negotiation, pagination/filter/sort, versioning policy, conditional requests/ETags, cache headers and optional idempotency storage. A sample tests conflicting updates and duplicate mutations. | API-17, API-22, API-23; API-29 for durable idempotency |
| API-25 | P1 / P | OpenAPI/JSON Schema contract import/export, examples, security schemes and compatibility tests. Pin a supported spec version; explicit schema registration works before automatic derivation or generated SDKs. | API-13, API-22, API-23 |
| API-26 | P1 / P | Cookies, sessions, CORS, CSRF defense where cookie auth is used, security headers and trusted-proxy handling. Test secure cookie attributes, origin allowlists and forged forwarding headers. | API-11, API-22, API-27; API-36 only for distributed sessions |

Error responses should interoperate with
[Problem Details (RFC 9457)](https://www.rfc-editor.org/rfc/rfc9457.html).
Cache behavior follows [RFC 9111](https://www.rfc-editor.org/rfc/rfc9111.html).
[OpenAPI 3.1.1](https://spec.openapis.org/oas/v3.1.1.html) is a concrete starting
reference, not a claim that it is the latest version or an already supported format.

### Application security and resilience — roadmap 6.16

| ID | Priority / home | Deliverable and completion criterion | Dependencies |
|----|-----------------|--------------------------------------|--------------|
| API-27 | P1 / P | Authn/authz providers: API keys, password hashing when needed, verified JWT/JWK rotation, OAuth/OIDC integration, claims/scopes and resource/tenant authorization. Test expiry, issuer/audience/algorithm policy, revoked sessions and cross-tenant denial. | API-03, API-09, API-11, API-13, API-18, API-22 |
| API-28 | P1 / P | Resilience: request quotas, rate limiting, concurrency budgets, retry/backoff/jitter, circuit breaking, SSRF-aware egress policy and webhook signing/replay protection. Bound memory and retry budgets; never imply exactly-once network delivery. | API-03, API-08, API-09, API-18, API-22; API-36 for distributed limits |

Authentication integrations should use the
[OAuth security BCP](https://www.rfc-editor.org/rfc/rfc9700.html) and
[JWT BCP](https://www.rfc-editor.org/rfc/rfc8725.html) as review inputs. They do
not require Ofir to ship its own identity provider or cryptographic algorithms.

### Databases and persistence — roadmap 6.17

| ID | Priority / home | Deliverable and completion criterion | Dependencies |
|----|-----------------|--------------------------------------|--------------|
| API-29 | P0 / P | SQL driver contract: connection, parameter binding, typed/null values, rows/cursors, errors and deterministic close. Keep driver capabilities visible; parameterized-query and row-lifetime tests pass. | API-01, API-06, API-08; 5.3 for shared interfaces |
| API-30 | P0 / P | SQLite adapter for embedded/local services and tests, prepared statements, transaction control and busy/lock handling. Verify rollback, resource cleanup and concurrent readers/writer contention. | API-02, API-29; SQLite provider/build ADR |
| API-31 | P1 / P | PostgreSQL adapter: TLS/authentication, prepared statements, SQLSTATE errors, typed rows including decimal/UUID/date/JSON, streaming and cancellation. Integration tests cover disconnects, server errors and null/precision fidelity. | API-04, API-05, API-11, API-13, API-29; provider/build ADR |
| API-32 | P1 / P | Bounded connection pools and transaction scopes: acquire deadlines, rollback-on-drop, savepoints, isolation and broken-connection eviction. A cancelled/failed query cannot return a busy or aborted connection as healthy. | API-08, API-12, API-29, API-30 or API-31 |
| API-33 | P0 / P | Versioned SQL migrations: checksums, ordering, migration locking, status/plan command and explicit irreversible steps. Test empty and existing schemas plus failure recovery; use driver capabilities for transactional DDL. | API-02, API-29, API-30; API-31 integration before production |
| API-34 | P2 / P | Query builder, row mapping and optional ORM with explicit SQL escape hatch. Parameterization, generated queries, pagination and transaction ownership remain inspectable; no mandatory ORM dependency. | API-14, API-29; 6.7a only for compile-time query generation |
| API-35 | P2 / P | Additional SQL adapters, starting with MySQL/MariaDB if demanded; SQL Server follows actual usage. Publish capability/type/isolation matrices and run real-server tests per adapter. | API-29, API-32, API-47 |
| API-36 | P1 / P | Cache interface, bounded in-process TTL cache and optional Redis adapter with TLS, pooling and explicit invalidation. Test expiry, unavailable-cache behavior and stale data; cache outages do not silently change authorization. | API-03, API-10, API-11, API-12; API-32 pool conventions |
| API-37 | P2 / P | Document/key-value/search adapters (for example MongoDB and an external search service) with explicit consistency, pagination and retry capabilities. Each adapter needs contract and integration tests; no fake universal SQL abstraction. | API-08, API-10, API-11, API-13, API-47 |

SQLite and PostgreSQL are the proposed first pair: embedded reproducible tests
and a networked service database. This is a sequencing recommendation, not a
final driver implementation choice. Evaluate reviewed C bindings through the
existing FFI before writing new wire protocols. PostgreSQL provides
[parameterized execution](https://www.postgresql.org/docs/current/libpq-exec.html)
and [asynchronous operations](https://www.postgresql.org/docs/current/libpq-async.html).
SQLite's [transaction rules](https://www.sqlite.org/lang_transaction.html)
require tests for single-writer contention and savepoints rather than pretending
that transaction semantics are identical between drivers.

### Integration and realtime — roadmap 6.18

| ID | Priority / home | Deliverable and completion criterion | Dependencies |
|----|-----------------|--------------------------------------|--------------|
| API-38 | P2 / P | Object storage adapter, streamed uploads/downloads, checksums, signed URLs and optional local backend. Test partial uploads, retries, cancellation, quotas and credential redaction. | API-02, API-09, API-11, API-18 |
| API-39 | P2 / P | Durable jobs, scheduling and queue/broker adapters (e.g. Redis, RabbitMQ, Kafka). Document delivery guarantees, acknowledgement, retry/dead-letter policy, idempotency and transactional outbox. Test crashes and duplicate deliveries; timezone-aware schedules use API-04. | API-03, API-08, API-12, API-29; backend-specific transport |
| API-40 | P2 / P | SSE and WebSocket packages: reconnect/heartbeats, bounded per-client buffering, authentication and slow-consumer policy. Disconnect/cancellation and protocol interoperability tests pass. | API-08, API-11, API-19, API-27 |
| API-41 | P2 / P | Optional service adapters: gRPC/Protobuf, GraphQL, SMTP/email and provider SDKs. Select each from actual application needs, with independent schemas, limits, version policy and integration tests. | API-14, API-18, API-22; API-20 for gRPC over HTTP/2 |

### Observability — roadmap 6.19

| ID | Priority / home | Deliverable and completion criterion | Dependencies |
|----|-----------------|--------------------------------------|--------------|
| API-42 | P0 / S+P | Structured logging facade: levels, key/value fields, JSON/text sinks, request IDs, redaction and bounded asynchronous sinks. File rotation is an optional sink; failure/flush/shutdown behavior is tested. | API-01, API-03, API-06; API-13 for JSON; API-12 for asynchronous sinks |
| API-43 | P1 / P | Metrics: counters/gauges/histograms, request and database pool instrumentation, latency/error/saturation measures and exporter adapters. Enforce cardinality budgets; test concurrent updates and exporter failures. | API-03, API-12, API-19, API-32 |
| API-44 | P1 / P | Distributed tracing: request context propagation, spans, sampling, HTTP/SQL instrumentation and OpenTelemetry export. Test cross-service parentage, redaction, queue limits and shutdown flush. | API-08, API-18, API-19, API-31, API-42 |

Observability uses the concepts of separate
[signals](https://opentelemetry.io/docs/concepts/signals/) and
[context propagation](https://opentelemetry.io/docs/concepts/context-propagation/).
Logging a request ID alone does not complete metrics or distributed tracing.

### Testing and portability — roadmap 6.20

| ID | Priority / home | Deliverable and completion criterion | Dependencies |
|----|-----------------|--------------------------------------|--------------|
| API-45 | P0 / P | Test utilities: in-process HTTP client/server, ephemeral ports, fake clocks/DNS/entropy, temporary storage, database fixtures and schema/response assertions. Tests run offline by default; integration tests name required local services. | API-02, API-03, API-13, API-18, API-19, API-30 |
| API-46 | P1 / tooling | Protocol corpus/fuzz tests, TLS/SQL interoperability, leak/race checks, fault injection, load/soak tests and Linux/macOS/Windows coverage. Publish throughput, p50/p95/p99, allocations/RSS, open handles and overload behavior with reproducible conditions. | API-11, API-12, API-17, API-31, API-45; 6.8 |

### Distribution and reference service — roadmap 6.21

| ID | Priority / home | Deliverable and completion criterion | Dependencies |
|----|-----------------|--------------------------------------|--------------|
| API-47 | P1 / tooling+P | Publish reproducible framework/driver packages: lockfiles, native-library discovery/linking, licenses, dependency provenance/SBOM and an update/advisory policy. A clean machine can build from the documented toolchain. | 6.5; provider ADRs |
| API-48 | P1 / P+docs | Deployment lifecycle: separate liveness/readiness/startup checks, graceful drain, bounded shutdown, config/secret injection, non-root/container examples and reverse-proxy trust policy. Rolling restart and database outage scenarios are exercised. | API-07, API-08, API-11, API-19, API-32, API-42, API-43 |
| API-49 | P0 local / P1 production / example | Reference CRUD service with validation, JSON, filesystem upload, migrations, SQLite locally and PostgreSQL in integration, timestamps, structured logs and documented API. Production variant adds TLS, authz, metrics/traces and shutdown tests; measure it as a whole. | Completion gates below |

### Operational follow-ups — roadmap 6.21a

These tasks add acceptance evidence for production claims. They can be
implemented independently after the named foundations and do not expand API-F
or API-L. Select which are required for a particular API-P deployment.

| ID | Priority / home | Deliverable and completion criterion | Dependencies |
|----|-----------------|--------------------------------------|--------------|
| API-50 | P1 / tooling+P | Run published client versions against a changed service. Classify wire-format, schema and behavioral breaks; publish a compatibility report and migration path for intentional breaks. | API-24, API-25, API-45, API-49 |
| API-51 | P1 / tooling+P | Restore a production-shaped backup into a clean environment, apply migrations, verify data integrity and record recovery time. Exercise partial and corrupt backups without modifying the source database. | API-29, API-31, API-33, API-49 |
| API-52 | P1 / P+docs | Define and test retention/deletion across database, cache, object storage and logs for the reference workload. Verify expiration, deletion propagation and documented exceptions without exposing private data in traces. | API-29, API-36, API-38 when used, API-42, API-49 |
| API-53 | P1 / P | Rotate database and TLS credentials while requests are in flight. Renew pooled connections and certificates without leaking secrets or dropping accepted requests; record rollback behavior. | API-07, API-11, API-31, API-32, API-48 |
| API-54 | P1 / P+tooling | Test tenant isolation under load and injected database/cache/trace failures. A tenant cannot read another's data or consume its quotas; logs and traces retain correct tenant context. | API-27, API-28, API-32, API-36 when used, API-44, API-46 |

JSON follows [RFC 8259](https://www.rfc-editor.org/rfc/rfc8259.html); timestamps
use [RFC 3339](https://www.rfc-editor.org/rfc/rfc3339.html), and UUID formats
use [RFC 9562](https://www.rfc-editor.org/rfc/rfc9562.html). Exact decimal and
missing/null policies must survive both serialization and database adapters.

## Delivery gates and recommended order

These are ecosystem milestones independent of language milestones M1-M5.
They do not silently expand the language 1.0 release promise.

1. **API-F: foundations.** API-01/02/03/06/08/09/10/12/13/15 have portable
   contracts and passing targeted tests. Pure codec, time and filesystem work
   can begin while 6.1 and interfaces are still in progress.
2. **API-L: local REST service.** API-F plus API-07/17/18/19/22/23/24/29/30/33/42/45
   and the local API-49 example. Exercise CRUD, validation errors, migration
   failure, streamed file upload, request deadlines and graceful shutdown.
   This gate is suitable for local development; it does not certify deployment.
3. **API-P: production candidate.** API-L plus API-04/05/11/14/16/25/26/27/28/31/32/
   43/44/46/47/48 and the production API-49 variant. Run the reference workload
   under failures, load and restart; publish resource/latency results and
   supported platform/provider versions. Add API-36 when cache/distributed
   session or quota storage is part of that deployment.
4. **API-X: optional extensions.** HTTP/2 and HTTP/3, additional drivers, ORM,
   object storage, jobs, realtime and other protocol/provider packages are
   accepted separately with their own contract and interoperability tests.

A local service can use explicit codecs/routes and direct SQLite connections.
Auto-derivation, dynamic dispatch, ORM and async are not hidden dependencies
of API-L. The production gate adds connection pooling and the appropriate
security/operations contracts before deployment claims.

## Decisions to record before implementation

| Decision | Resolve before | Required evidence |
|----------|----------------|-------------------|
| Public module ownership and stability boundary | First new public module | ADR assigning S vs P, naming, errors and compatibility policy |
| Stream, cancellation and blocking/async interoperability | API-01/08/10 | Handle lifetime and interruptibility prototypes on supported OSes |
| TLS/crypto provider | API-09/11 | Certificate validation, updates, license, static/dynamic linking and test matrix |
| SQLite/PostgreSQL provider | API-30/31 | FFI ownership, cancellation, threading, error/type mapping and packaging prototype |
| Timezones, decimals and schema evolution | API-04/05/14 | Serialization/storage round trips and version/update policy |
| Framework and security state | API-22/26/27 | Explicit resource lifetime, tenant boundaries and middleware order tests |
| Production performance targets | API-46/API-P | Fixed workload, hardware/toolchain, baseline, latency and resource budgets |

Every implementation PR should link its API ID, specify its public contract,
update the relevant `docs/std` or package documentation, and add focused tests.
A prototype, a design document or one successful request does not close a row.
