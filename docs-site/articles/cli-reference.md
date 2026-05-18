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
| `zero build <input>` | Emit an executable, object file, or WebAssembly module. |
| `zero ship <input>` | Produce a release preview with signed checksums. |
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
zero build --emit wasm --target wasm32-wasi examples/direct-wasm-add.0 --out .zero/out/add.wasm
zero graph --json examples/systems-package
zero size --json examples/point.0
zero new package hello
zero ship --json --target linux-musl-x64 hello --out .zero/ship/hello
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
| `zero check --json` | Diagnostics with code, span, expected/actual details, help, and repair metadata. |
| `zero graph --json` | Modules, public symbols, capabilities, static facts, and helper use. |
| `zero dev --json` | A watch plan for changed source, manifest, package-lock, and generated-binding inputs. |
| `zero dev --json --trace` | Adds phase timing, cache hit/miss facts, diagnostics passthrough, and `interfaceFingerprints`. |
| `zero time --json` | Compiler phase timing plus `interfaceFingerprints` and incremental invalidation facts. |
| `zero build --json` | Artifact path, size, selected `toolchain`, target triple, linker flavor, and sysroot status. |
| `zero size --json` | `profileSemantics`, `profileCatalog`, `profileBudget`, `sizeBreakdown`, `retentionReasons`, and `optimizationHints`. |
| `zero ship --json` | A release preview with artifact names, hashes, a checksum file and signature, debug-symbol metadata, size report, and SBOM placeholder. |
| `zero doctor --json` | Host checks plus `targetToolchains`, the per-target readiness matrix. |

`zero check --json` and `zero graph --json` also include `compileTime`.
That object records bounded `meta` evaluation, sandbox denials, cache key
inputs, typed reflection facts, and integer/Bool/enum static values.

Build and ship JSON include `releaseTargetContract`. It records artifact kind,
object format, direct linker flavor, target libc mode, sysroot requirements,
emitter readiness, target capability facts, and the repeat-build hash policy.
`zero ship --json` nests the same contract under
`releasePreview.targetContract`.

## Build Outputs

| Emit mode | Command |
| --- | --- |
| Native executable | `zero build --emit exe --target linux-musl-x64 <input>` |
| Native object | `zero build --emit obj --target linux-musl-x64 <input>` |
| WebAssembly | `zero build --emit wasm --target wasm32-wasi <input>` |

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

## Keys

`zero keys` manages developer keys and the signed trust list used for ledger
verification.

Keys live in `$HOME/.zero/keys` (override `ZERO_KEYS_DIR`). The trust list lives
in `$HOME/.zero/trust` as `trusted-keys.json` and `trusted-keys.sig`. Override
that location with `ZERO_TRUSTED_KEYS_DIR` or point directly at files with
`ZERO_TRUSTED_KEYS` and `ZERO_TRUSTED_KEYS_SIG`.

`zero ship` requires `ledger.json` and a signer key authorized by the ledger.
Pass `--insecure` to skip trusted-keys signature verification when offline.

Refresh the trust list with:

```sh
zero keys refresh
```

Override download locations with `ZERO_TRUSTED_KEYS_URL` and
`ZERO_TRUSTED_KEYS_SIG_URL`.

Common commands:

```sh
zero keys list
zero keys show default
zero keys new --email dev@example.com --label default
zero keys import --public <hex> --email maintainer@example.com --label backup
zero keys use default
zero keys authorize --ledger ledger.json --key backup
zero keys revoke --ledger ledger.json --key backup --reason "left"
zero keys rotate --ledger ledger.json --old backup --new third
zero keys refresh
```

## Skills

`zero skills` serves bundled skill content for agents:

```sh
zero skills list
zero skills get zero
zero skills get zero --full
zero skills path zero
```

Add `--json` for automation. Set `ZERO_SKILLS_DIR` to point the command at an
alternate skill directory.

## Language Server Smoke

Run the editor smoke path with:

```sh
npm run zls -- --self-test
```

The smoke covers diagnostics, hover docs, completions, go-to definition,
document symbols, and quick-fix code actions surfaced from `zero fix` for
`TAR002`, `TYP009`, `ERR002`, `ERR003`, and `PUB001`.

## Utility Commands

```sh
zero --version [--json]
zero new cli|lib|package <path>
zero keys <command> [options]
zero keys refresh
zero doctor [--json]
zero check [--json] [--target <target>] <input>
zero dev [--json] [--trace] [--target <target>] <input>
zero run [--target <target>] [--profile dev|release] [--out <file>] <input> [-- args...]
zero build [--emit exe|obj|wasm] [--target <target>] [--profile dev|release] [--out <file>] <input>
zero ship [--json] [--target <target>] [--profile release-small|tiny|audit] [--out <file>] <project|zero.json>
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
zero routes --json <project>
zero abi check|dump [--json] [--target <target>] <input>
zero tokens --json <input>
zero parse --json <input>
```
