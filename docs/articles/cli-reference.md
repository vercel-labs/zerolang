## CLI Reference

`zero` checks, formats, runs, tests, builds, inspects, and repairs Zero programs.

Most commands accept the same input forms:

| Input | Meaning |
| --- | --- |
| `file.0` | A single Zero source file. |
| `project/` | A package directory containing `zero.json`. |
| `zero.json` | A package manifest. |

## Daily Commands

| Command | Use it for |
| --- | --- |
| `zero check <input>` | Parse, typecheck, and report diagnostics. |
| `zero run <input>` | Build and run a host executable. |
| `zero test <input>` | Run inline `test` blocks. |
| `zero fmt <input>` | Print formatted source. Add `--check` in CI. |
| `zero build <input>` | Emit an executable or object file. |
| `zero ship <input>` | Produce a release preview with checksums and metadata. |
| `zero graph <input>` | Inspect modules, symbols, capabilities, and helper use. |
| `zero size <input>` | Explain artifact size, retained helpers, and profile budgets. |
| `zero doc <input>` | Emit public API documentation facts. |
| `zero fix --plan --json <input>` | Ask for a typed repair plan. |
| `zero doctor` | Check host and target readiness. |

Copyable examples:

```sh
zero check examples/hello.0
zero run examples/add.0
zero test conformance/native/pass/test-blocks.0
zero build --emit exe --target linux-musl-x64 examples/add.0 --out .zero/out/add
zero graph --json examples/systems-package
zero size --json examples/point.0
zero ship --json --target linux-musl-x64 examples/hello.0 --out .zero/ship/hello
zero doctor --json
```

## Run

`zero run` builds a host executable with the direct backend, runs it, passes
through program stdout/stderr, and exits with the program status.

Pass program arguments after `--`:

```sh
zero run examples/cli-file.0 -- input.txt
```

## JSON Output

Use `--json` when another tool will read the result. Text output is for people.

| Command | Useful JSON fields |
| --- | --- |
| `zero check --json` | Diagnostics with code, span, expected/actual details, help, repair metadata, and `targetReadiness` for the selected target/emit kind. |
| `zero graph --json` | Modules, public symbols, capabilities, static facts, and helper use. |
| `zero dev --json` | A watch plan for changed source, manifest, package-lock, and generated-binding inputs. |
| `zero dev --json --trace` | Adds phase timing, cache hit/miss facts, diagnostics passthrough, and `interfaceFingerprints`. |
| `zero time --json` | Compiler phase timing plus `interfaceFingerprints` and incremental invalidation facts. |
| `zero build --json` | Artifact path, size, selected `toolchain`, target triple, linker flavor, sysroot status, and runtime provider facts when a helper such as hosted HTTP is linked. |
| `zero size --json` | `profileSemantics`, `profileCatalog`, `profileBudget`, `sizeBreakdown`, `retentionReasons`, and `optimizationHints`. |
| `zero ship --json` | A release preview with artifact names, hashes, a checksum file, debug-symbol metadata, size report, and SBOM placeholder. |
| `zero doctor --json` | Host checks plus `targetToolchains`, the per-target readiness matrix. |

`zero check --json` and `zero graph --json` also include `compileTime`.
That object records bounded `meta` evaluation, sandbox denials, cache key
inputs, typed reflection facts, and integer/Bool/enum static values.

`zero check --json --target <target> --emit <kind>` keeps language validity
separate from target buildability. Top-level `ok` and `diagnostics` describe
parse/typecheck results; `targetReadiness.ok`, `buildable`, and nested
diagnostics describe predictable backend blockers without writing artifacts.

Build and ship JSON include `releaseTargetContract`. It records artifact kind,
object format, direct linker flavor, target libc mode, sysroot requirements,
emitter readiness, target capability facts, and the repeat-build hash policy.
Host builds that retain runtime-backed helpers can also include `objectBackend`
linking facts such as retained runtime objects, provider libraries, and
`httpRuntime` TLS/provider metadata.
`zero ship --json` nests the same contract under
`releasePreview.targetContract`.

## Build Outputs

| Emit mode | Command |
| --- | --- |
| Native executable | `zero build --emit exe --target linux-musl-x64 <input>` |
| Native object | `zero build --emit obj --target linux-musl-x64 <input>` |

Removed backend flags report `BLD003`. Use direct emitters; the removed C
backend is not a compatibility path.

## Tests

`zero test --json` is shaped for CI and editors. It reports:

- discovery: `testDiscovery`, `fixtures`, `snapshotKey`
- counts: `discoveredTests`, `selectedTests`, `passedTests`, `failedTests`
- expected failures: `expectedFailures`, `unexpectedPasses`
- execution: `targetFacts`, `results`, `durationMs`, `stdout`, `stderr`

Expected-fail tests use `xfail:`, `expected fail:`, or `[xfail]` in the test
name. A test marked this way must fail; an unexpected pass fails the command.

## Skills

`zero skills` serves bundled skill content for agents:

```sh
zero skills list
zero skills get zero
zero skills get language
zero skills get zero --full
```

Add `--json` for automation. Skill content is bundled with the compiler so
agents can load the workflow that matches the Zero binary they are using.

## Language Server Smoke

Run the editor smoke path with:

```sh
pnpm run zls -- --self-test
```

The smoke covers diagnostics, hover docs, completions, go-to definition,
document symbols, and quick-fix code actions surfaced from `zero fix` for
`TAR002`, `TYP009`, `ERR002`, `ERR003`, and `PUB001`.

## Utility Commands

```sh
zero --version [--json]
zero new cli|lib|package <path>
zero doctor [--json]
zero check [--json] [--target <target>] [--emit exe|obj] <input>
zero dev [--json] [--trace] [--target <target>] <input>
zero run [--target <target>] [--profile dev|release] [--out <file>] <input> [-- args...]
zero build [--emit exe|obj] [--target <target>] [--profile dev|release] [--out <file>] <input>
zero ship [--json] [--target <target>] [--profile release-small|tiny|audit] [--out <file>] <input>
zero test [--json] [--filter <name>] [--target <target>] [--cc <path>] [--out <file>] <input>
zero fmt [--check] <input>
zero graph [--json] [--target <target>] <input>
zero doc [--json] [--target <target>] <input>
zero size [--json] [--target <target>] [--out <artifact>] <input>
zero explain [--json] <diagnostic-code>
zero fix --plan --json [--target <target>] <input>
zero targets
zero clean [--all]
zero mem [--json] [--target <target>] <input>
zero time --json [--target <target>] <input>
zero abi check|dump [--json] [--target <target>] <input>
zero tokens --json <input>
zero parse --json <input>
```
