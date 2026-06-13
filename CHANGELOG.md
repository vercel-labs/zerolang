# Changelog

## 0.3.4

<!-- release:start -->

- Makes graph patching the primary agent edit loop with stdin patch bodies, `--replace-in-fn`, expression replacement by handle, structural rewrites, declaration-level edits, helper/test patch ops, and inline handle diagnostics across view, query, and patch commands.
- Improves graph/source reconciliation with patch-specific stale-refresh tips, once-per-state notes, accepted whole-file rewrites when function sets are preserved, and clearer unsupported graph-test and non-entry `World` helper diagnostics.
- Speeds up graph workflows with memoized repository store loads, adjacency orders, semantic check verdicts, static const classification, pre-merge MIR cache keys, and reusable runtime object cache artifacts.
- Reduces direct backend output and broadens native coverage with constant array-fill loop lowering, COFF x64 HTTP request-body extraction, chunked file reads via `std.fs.readBytesAt`, and total-file-size reporting for fixed-buffer reads.
- Refreshes bundled skills, CLI help, docs, command contracts, conformance fixtures, compiler metrics, and validation guidance around the patch-first agent workflow.
- Hardens release checks with runtime object cache cleanup, graph patch routing coverage, examples gate teardown retries, and additional package cache/version assertions.

### Contributors

- @ctate

<!-- release:end -->

## 0.3.3

- Makes package graph stores refresh from edited projections across compiler commands, classifies source/store sync by content instead of mtimes, and gives stale, diverged, and ambiguous reconcile states deterministic diagnostics and repairs.
- Improves agent-facing inspection and edit loops with scoped `zero query` output, `zero view --fn`, `zero diff --fn`, `zero patch --replace-fn --body-file`, focused command help, and smaller version-matched skill topics with section-scoped stdlib fetches.
- Expands standard-library coverage with `std.regex`, `std.inet`, `std.unicode`, and RFC 3339 helpers in `std.time`, including docs, skill references, and runtime conformance fixtures.
- Hardens direct backend correctness for shape references, large stack frames, fallible `Void` mains, file reads, trap stderr, top-level consts, null `Maybe` locals, fixed-array lengths, and untyped integer literal adoption.
- Speeds up large graph workflows with indexed reconciliation, stdlib merges, projection validation caches, graph lowering indexes, and borrow-contract scans while preserving deterministic graph hashes and artifacts.
- Adds graph-store diagnostic codes and complete `zero explain` coverage, buildability gating for unsupported checked constructs, example package gates, sanitizer smoke, package build determinism contracts, and git build hashes in `zero --version`.

### Contributors

- @ctate

## 0.3.2

- Adds `break` and `continue` support to the direct backend and `zero test`, lowering both through typed MIR and all direct emitters with conformance coverage and required runtime checks.
- Speeds up graph validation with indexed node-hash tables, a reusable adjacency index, and deduplicated store validation, making `zero import` on large programs roughly 12x faster.

### Contributors

- @ctate

## 0.3.1

- Hardens compiler, runtime, and toolchain I/O boundaries with checked file reads and writes, directory rejection, atomic output staging, safer executable finalization, mapped-MIR and binary graph parser validation, and regular-file checks for generated compiler/linker outputs.
- Removes remaining shell-based native execution surfaces in favor of argv-based process helpers, safer tool resolution, guarded child setup, validated compiler overrides, and explicit metrics coverage for process and shell-call regressions.
- Expands and hardens the real `std.http.listen` runtime with per-request spooling, private temp lifecycles, oversized request handling, socket deadlines, structured JSON error responses, and native smoke coverage for listener behavior.
- Grows graph-backed standard-library coverage with environment, filesystem, process, time, random, crypto, URL, JSON, HTTP text/HTML, redirect, array writer, object writer, CLI argument, and HTTP JSON error helpers.
- Improves graph row patch authoring with grouped expressions, compact casts and checks, logical OR, else-if branches, bare returns, natural call arguments, stronger invalid-row diagnostics, and refreshed bundled skill guidance.
- Adds graph build performance instrumentation and budgets, stdlib graph merge caching, agent-scale and Rosetta challenge evals, parallel CI validation jobs, sharded native checks, deep validation profiles, and local aggregate agent checks.

### Contributors

- @ctate

## 0.3.0

- Makes graph-first package authoring the normal workflow: repository `zero.graph` stores are the compiler input, `.0` files are human-readable projections, `zero init` owns project creation, `zero diff` supports graph review, and source projection inputs are rejected at the compiler boundary.
- Adds binary repository graph stores and graph-backed standard-library modules, with source-free status/verify, import/export, merge, query, patch, source-map, reconciliation, metadata, drift detection, and checked-in binary graph fixtures for examples, stdlib, and conformance.
- Advances graph-native compiler execution across check, build, run, test, size, doc, dev, time, package dependency, benchmark, and validation paths, including typed graph-to-MIR lowering, mapped final-MIR cache reuse, graph semantic/resolution/ownership facts, and graph-native test execution.
- Introduces explicit experimental LLVM backend support with backend selection contracts, textual LLVM IR emission, host executable builds, toolchain diagnostics, profile facts, and no-fallback release policy.
- Expands web API support in `std.http` with routing, JSON response/body validation, CORS, OPTIONS, path segment, bearer token, cookie/session helpers, HTTP listener/client graph lowering, and CRM/router/ping-pong examples.
- Hardens direct backend and compiler internals with C API contracts, unsafe C link validation, target manifest matching, ABI layout tables, direct emit-kind handling, buildability value/target checks, TOML manifests, and stricter metrics guardrails.
- Refreshes public docs, README, bundled skills, graph-first examples, command contracts, conformance, docs tests, aggregate validation, and sandboxed conformance workers around the current agent-facing workflow.

### Contributors

- @ctate

## 0.2.1

- Adds extern C call support with target-aware header preprocessing, stricter link-plan validation, direct object linking, graph metadata fixes, and diagnostics for missing or unsafe C import inputs.
- Expands the standard library across memory, collections, search, sort, ASCII, formatting, text, parsing, math, random, time, codec, JSON, URL, hosted I/O, filesystem, HTTP, testing, and logging helpers.
- Strengthens memory-safety diagnostics for `Maybe` guards, owned moves, mutable span aliases, span lifetime escapes, aggregate reassignment, scalar match fallthrough, and exported safety facts.
- Improves direct backend correctness across Mach-O, ELF64, COFF, x64, and AArch64 paths, including byte-view bounds, checksum helpers, open slices, usize runtime returns, and indexed store handling.
- Retires row source parsing from the public source boundary, keeps canonical `.0` text as the supported input surface, and rejects stale row graph artifact paths.
- Refreshes docs, examples, skills, command contracts, conformance fixtures, stdlib contracts, target-matrix checks, compiler metrics, and reliability smoke coverage around the current workflows.

### Contributors

- @ctate
- @ihasq
- @PeterXMR

## 0.2.0

- Makes canonical `.0` text the native source surface, with a parser, formatter, Program import path, diagnostics, docs, examples, stdlib sources, benchmarks, conformance fixtures, and command snapshots aligned around that format.
- Promotes ProgramGraph from inspection output into an editable artifact workflow with deterministic dump, validate, view, check, roundtrip, import, patch, build, run, test, size, and package entrypoint support.
- Adds source-backed graph edits so checked graph patches can rewrite canonical `.0` files while preserving package/import context, std helper context, metadata, and stable graph identities.
- Expands direct backend coverage with darwin-x64 Mach-O executable support, shared AArch64 ELF/COFF emission, target-readiness reporting, byte-view ABI fixes, and stronger cross-target buildability checks.
- Adds source-backed `std.path`, `std.str`, and `std.math` modules with docs, examples, Rosetta coverage, and direct target validation.
- Exposes resolved call-resolution and stdlib helper facts in graph JSON, tightening generic return typing, member call facts, std helper validation, and package cache metadata.
- Grows Rosetta and compiler smoke coverage, graph command contracts, metrics budgets, CLI docs, and skills around the current agent edit loop.

### Contributors

- @ctate

## 0.1.4

- Adopts canonical `.0` text as the current Zero source surface across parsing, import resolution, package manifests, command workflows, docs, fixtures, formatting, and artifact contracts.
- Rebuilds checker internals around TypeCore, binder-aware unification, generic inference, static interface validation, provenance substitution, and shared call-resolution facts.
- Hardens direct emitters and native artifacts through MIR verifier contracts, direct specialization metadata, target buildability checks, and shared x64, AArch64, ELF, Mach-O, and COFF emission helpers.
- Adds clearer direct backend selection, target readiness, and buildability reporting so command JSON and failed native builds expose deterministic blocker facts.
- Removes stale wasm, compiler-zero, and superseded parser surfaces while refreshing public docs, README guidance, and zerolang branding around the current system.
- Strengthens compiler structure guardrails, metrics budgets, TypeScript-based repo scripts, docs build infrastructure, CI workspace commands, and eval harness isolation.
- Renames bundled version-matched skills to the current flat skill names while keeping `zero skills` discovery focused on the native compiler.

### Contributors

- @ctate

## 0.1.3

- Adds hosted HTTP client runtime support, including runtime packaging fixes, JSON byte helpers for wasm, HTTP final-header parsing fixes, and direct array span lowering repairs.
- Parses `use` declarations into syntax graph facts and reports import diagnostics, fix plans, and graph edges against the specific import source ranges.
- Adds structured backend blocker and target-readiness facts to command JSON so checks, build previews, and direct backend failures explain unsupported targets more deterministically.
- Expands agent-facing diagnostics with BOR001 borrow trace facts, target-neutral CGEN004 wording, and NAM004 generic type-name shadowing rejection.
- Hardens native compiler allocation paths across shared buffers, parser, checker, IR lowering, emitters, source handling, targets, and driver state for deterministic allocation failures.
- Updates the public site and docs with current guidance, docs chat rate limiting, and pnpm-based repository workflows.
- Restores native versioned skill guidance, keeps the repo wrapper on the native compiler, and removes the legacy `zero skills path` command.

### Contributors

- @ctate
- @PeterXMR
- @h4ckf0r0day

## 0.1.2

- Rebuilds borrow provenance tracking across references, fields, subpaths, assignments, control-flow joins, receiver side effects, generic methods, return summaries, and unreachable paths.
- Tightens borrow conflict checking by deriving conflicts from provenance and comparing concrete places, with additional conformance coverage for provenance joins and edge cases.
- Fixes checker regressions around borrow origins, reassignment, aggregate values, explicit reference fields, unknown identifiers, and fallibility propagation through wrappers.
- Fixes dynamic CLI strings and Darwin executable UUID emission.
- Adds Apache-2.0 licensing and lockfile license metadata.
- Updates the public site with an install toggle, GitHub star count, and a mobile header Docs link.

### Contributors

- @ctate
- @badlogic
- @chenrui333
- @heylakatos
- @onevcat

## 0.1.1

- Adds the public installer at `https://zerolang.ai/install.sh`, with platform selection, GitHub release downloads, checksum verification, and `$HOME/.zero/bin/zero` installation.
- Adds `zero run` for the everyday edit loop: build a host executable, run it, pass program arguments after `--`, forward stdout/stderr, and return the program exit status.
- Updates README, homepage, getting started, install, and CLI docs around the curl install path, copyable commands, and `zero run`.
- Reworks public docs to be more scannable and current, including stronger language, diagnostics, testing, target, package, optimization, and standard library references.
- Removes placeholder module docs that described surfaces not ready for users and adds current module docs for `std.crypto`, `std.http`, and `std.net`.
- Adds version-matched agent guidance through `zero skills`, including focused workflows for Zero syntax, diagnostics, builds, packages, standard library use, testing, and agent edit loops.
- Keeps the installable Zero skill as a thin bootstrap so external skill managers discover one Zero skill while the compiler serves the richer guidance for the installed version.
- Updates the `zero skills` CLI contract to serve bundled flat skill data while preserving list, get, path, and JSON workflows.

### Contributors

- @ctate
- @mvanhorn

## 0.1.0

- Initial public release of Zero as the programming language for agents.
- Includes the native compiler, examples, documentation site, and validation fixtures.
- Supported workflows use direct Zero emitters for the documented examples and targets.
