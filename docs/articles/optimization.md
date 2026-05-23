## Build profiles

Zero exposes six catalog profiles:

| Profile | Profile key | Purpose |
| --- | --- | --- |
| `debug` | `debug` | Development profile with diagnostics and local debugging support. |
| `dev` | `dev` | Development-oriented profile accepted by the CLI catalog. |
| `release-fast` | `fast` | Release profile biased toward throughput. |
| `release-small` | `small` | Release profile biased toward smaller output. |
| `tiny` | `tiny` | Minimal-output profile for strict size budgets. |
| `audit` | `audit` | Audit-oriented profile exposed by the CLI catalog. |

The CLI also accepts these source-backed aliases:

| Alias | Canonical profile |
| --- | --- |
| `fast` | `release-fast` |
| `small` | `release-small` |
| `release` | `release-small` |

Prefer `--profile <profile>` in documentation. `--release <profile>` is accepted for `build`, `run`, and `size` where command help documents it.

For shipping, use the documented `ship` profile form:

```sh
zero ship --profile release-small
zero ship --profile tiny
zero ship --profile audit
```

Copyable commands:

```sh
bin/zero build --json --profile small --target linux-musl-x64 examples/hello.0 --out .zero/out/hello-small
bin/zero build --json --profile tiny --target linux-musl-x64 examples/hello.0 --out .zero/out/hello-tiny
bin/zero size --json --profile debug --target linux-musl-x64 examples/memory-primitives.0
bin/zero size --json --profile tiny --target linux-musl-x64 examples/fixed-vec.0
bin/zero mem --json examples/allocator-collections.0
```

`zero build --json` includes:

- `profileSemantics`: canonical profile, `profileKey`, aliases, and optimization goal.
- `profileCatalog`: the available profiles and aliases.
- `profileBudget`: size limits and helper-budget policy for the selected profile.

`zero size --json` adds `sizeBreakdown`, `retentionReasons`, and
`optimizationHints`. Start there when an artifact is larger than expected.

`zero mem --json` is the memory-budget companion. It includes:

- `memoryBudgets`: stack, static, heap, arena, and fixed-buffer totals.
- `allocatorFacts`: which allocator APIs were used.
- `allocationInstrumentation`: allocation failure and cleanup facts.
- `collectionFacts`: fixed-capacity collection usage and no-global-allocator checks.

## Size Breakdown

`sizeBreakdown` is shaped for optimization workflows:

- `functions`: retained functions, tests, exports, estimated bytes, and retention reasons.
- `sections`: code, readonly literals, stack, debug metadata, and optional output artifact sections.
- `literals`: readonly string and byte literal cost.
- `stdlibHelpers`: pay-as-used helper cost and capability attribution.
- `imports`: capability imports retained by the selected program.
- `runtimeShims`: target/runtime shims retained by capabilities or bounds checks.
- `debugMetadata`: bytes and policy for profile-retained metadata.

Use `retentionReasons` to answer why a function, helper, literal, or debug
metadata block stayed in the artifact. Use `optimizationHints` for the next
action, such as switching away from `debug`, removing hosted filesystem helpers
from target-neutral code, or inspecting `topLargestEmittedHelpers`.

## Benchmark Trends

The benchmark gate writes both a full one-run report and a compact trend
artifact:

```sh
ZERO_BENCH_RUNS=1 pnpm run bench
```

The benchmark outputs are:

| File | Purpose |
| --- | --- |
| `.zero/bench/latest.json` | Full one-run smoke report. |
| `.zero/bench/trends/latest.json` | Compact trend summary. |
| `.zero/bench/trends/summary.md` | Human-readable companion report. |

The trend summary tracks artifact size, compressed size, build time, startup
time, operation timings, and peak RSS when available.

Use benchmark trends before trusting profile regressions. Profile builds and
size reports for `debug`, `fast`, `small`, and `tiny` should stay deterministic
across repeated runs.
