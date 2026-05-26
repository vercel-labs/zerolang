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
| `zero graph <input>` | Inspect modules, symbols, capabilities, helper use, and ProgramGraph facts. |
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
zero graph dump examples/hello.0
zero graph dump --out .zero/out/hello.graph examples/hello.0
zero graph validate .zero/out/hello.graph
zero graph view .zero/out/hello.graph
zero graph check --json .zero/out/hello.graph
zero graph size --json .zero/out/hello.graph
zero graph patch --out .zero/out/hello.patched.graph .zero/out/hello.graph .zero/out/hello.patch
zero graph roundtrip examples/hello.0
zero graph roundtrip .zero/out/hello.graph
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
| `zero graph --json` | Modules, public symbols, capabilities, static facts, helper use, and nested `programGraph`. |
| `zero graph dump --json` | The bare deterministic ProgramGraph with `moduleIdentity`, `graphHash`, validation, counts, nodes, and edges. Use `--out <file>` to write the dump artifact. |
| `zero graph validate --json` | A ProgramGraph artifact readback check with `moduleIdentity`, `graphHash`, counts, validation state, and optional canonical output path. |
| `zero graph view --json` | A generated Zero-shaped view for a ProgramGraph artifact with `moduleIdentity`, `graphHash`, `canonicalSource: false`, and optional output path. |
| `zero graph check --json` | Typecheck a ProgramGraph artifact through direct graph lowering with artifact identity, target, `check.lowering: "direct-program-graph"`, target readiness, diagnostics, optional generated-view output path, and an inline failed view when no output path was saved. |
| `zero graph size --json` | Size, helper, runtime, profile, and backend facts for a ProgramGraph artifact lowered through `direct-program-graph`, with graph identity and `canonicalSource: false`. |
| `zero graph patch --json` | Checked ProgramGraph artifact edits with graph-hash preconditions, per-operation node/field results, the changed graph hash, and optional canonical output path. |
| `zero graph roundtrip --json` | Source-to-graph-to-view or artifact-to-direct-lower stability with `semanticStable`, lowering mode, original and reparsed graph hashes, raw counts, normalized semantic counts, and optional output. |
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

## ProgramGraph Patches

`zero graph patch` applies checked edits to a saved ProgramGraph artifact and
prints or writes the canonical patched artifact. Patch files are line-oriented
text:

```text
zero-program-graph-patch v1
expect graphHash "graph:f76987e99677f1b3"
set node="node:000013" field="value" expect="hello from zero\n" value="hello patched\n"
insert node="node:patch001" kind="Literal" parent="node:000009" edge="arg" order="1" type="String" value="again\n"
rename node="node:000002" expect="main" value="start"
delete node="node:patch001"
```

The header is required. `expect graphHash` is optional but recommended; it
rejects edits against a different artifact. `set` requires `node`, `field`, and
`value`; `expect` is optional and rejects the operation when the current field
value differs.

Supported operations are `set`, `insert`, `insertEdge`, `replace`, `delete`,
and `rename`. `insert` creates a node and connects it to a parent node with an
ordered node edge. `insertEdge` connects existing graph facts across `node`,
`symbol`, `type`, or `effect` target domains. `replace` updates a node in
place and can require the current node hash through `expect`. `delete` removes
an owned subtree and rejects external references into that subtree. `rename`
updates a node name with an optional current-name precondition.

Editable scalar fields are `name`, `type`, `value`, `public`, `mutable`,
`static`, `fallible`, and `exportC`. Boolean fields accept only `true` or
`false`. `name` values must be identifier paths or supported operator tokens.
`type` values must be valid Zero type syntax. Strings support `\\`, `\"`,
`\n`, `\r`, `\t`, and `\u00XX` escapes for non-NUL bytes. NUL bytes are not
valid ProgramGraph patch text.

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
zero graph [dump|validate|view|check|size|patch|roundtrip] [--json] [--target <target>] [--out <file>] <input> [patch-file]
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
