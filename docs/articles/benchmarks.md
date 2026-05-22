## Benchmarks

The benchmark suite is a regression signal for Zero's goals: small artifacts,
fast builds, predictable output, and low overhead.

Run it locally:

```sh
pnpm run bench
```

The report is written to:

```sh
.zero/bench/latest.json
```

Trend artifacts are written to:

```sh
.zero/bench/trends/latest.json
.zero/bench/trends/summary.md
```

## Cases

The current cases include:

- `hello`: minimal output
- `add`: arithmetic and a helper function
- `structs`: type construction and direct field access
- `params`: multiple typed parameters and nested calls
- `buffers`: fixed arrays, spans, copy/fill, and byte equality
- `parser`: parser-like token metadata and direct predicate checks
- `codec`: `std.codec` loops, varint length, and CRC-32
- `parse`: scanner-style digit parsing
- `slices`, `arena`, `fallibility`, `branches`: memory views, fixed-buffer allocation style, explicit fallible branching, and branch-heavy loops
- `module-package`, `rescue`: package graph overhead and local rescue lowering
- `fs-resource`, `mem-copy-fill`, `zero-hash`: focused Zero cases for file-resource metadata, memory helpers, and deterministic byte payload processing

The Zero sources for these cases live under `benchmarks/zero`.
Host targets that do not support a direct executable runner for a case report it
as `skipped` with a reason instead of failing the whole benchmark run.

## Metrics

- `buildMs`: wall-clock time for the compiler command
- `runMs`: median wall-clock time across repeated executable runs
- `runMinMs`: minimum wall-clock time across repeated executable runs
- `runRuns`: raw per-run wall-clock timings
- `artifactBytes`: output executable size
- `compressedArtifactBytes`: gzip-compressed executable size
- `peakRssBytes`: maximum resident set size when available
- `expectedStdout` and `outputMatches`: semantic checks that catch drift

## Claims And Limits

Use these numbers narrowly:

- build time is represented by `buildMs`
- artifact size is represented by `artifactBytes`
- compressed size is represented by `compressedArtifactBytes`
- run time is represented by the per-case run metrics

Do not treat one local run as a definitive performance claim. Process startup,
host load, and cache state all affect results.

## Options

Change the repeat count:

```sh
ZERO_BENCH_RUNS=<n> pnpm run bench
```

Run the sandbox backend explicitly:

```sh
ZERO_BENCH_MODE=sandbox pnpm run bench
```

Sandbox mode prepares an isolated environment and is useful for CI. Local mode is
the default public workflow.
