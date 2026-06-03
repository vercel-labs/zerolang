## CLI Reference

`zero` checks, formats, runs, tests, builds, inspects, and repairs Zero programs.

Most commands accept the same input forms:

| Input | Meaning |
| --- | --- |
| `file.0` | Canonical Zero source text. |
| `project/` | A package directory containing `zero.json`. |
| `zero.json` | A package manifest. |

## Daily Commands

| Command | Use it for |
| --- | --- |
| `zero check <input>` | Parse, typecheck, and report diagnostics. |
| `zero run <input>` | Build and run a host executable with the selected backend. |
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
zero graph dump --out .zero/out/hello.program-graph examples/hello.0
zero graph import --out .zero/out/hello.program-graph examples/hello.0
zero graph inspect --json examples/hello.0
zero graph validate .zero/out/hello.program-graph
zero graph view examples/hello.0
zero graph view --out .zero/out/hello.view.0 .zero/out/hello.program-graph
zero graph source-map --json examples/hello.0
zero graph reconcile --json .zero/out/hello.program-graph --source examples/hello.0
zero graph check --json .zero/out/hello.program-graph
zero graph size --json .zero/out/hello.program-graph
zero graph build --json --emit obj --target linux-musl-x64 --out .zero/out/hello.o .zero/out/hello.program-graph
zero graph run .zero/out/hello.program-graph
zero graph test --json .zero/out/hello.program-graph
zero graph patch examples/hello.0 --expect-graph-hash graph:a7f7e6899a73f3b4 --op 'set node="#expr_653eeb6e" field="value" expect="hello from zero\n" value="hello patched\n"'
zero graph roundtrip examples/hello.0
zero graph roundtrip .zero/out/hello.program-graph
zero size --json examples/point.0
zero ship --json --target linux-musl-x64 examples/hello.0 --out .zero/ship/hello
zero doctor --json
```

## Run

`zero run` builds a host executable, runs it, passes through program
stdout/stderr, and exits with the program status. Direct output is the default;
pass `--backend llvm` for explicit LLVM host execution when `clang` is ready.

Pass program arguments after `--`:

```sh
zero run examples/cli-file.0 -- input.txt
zero graph run .zero/out/cli-file.program-graph -- input.txt
```

## JSON Output

Normal command output is designed to be readable by agents. Use `--json` when
another tool needs stable fields.

| Command | Useful JSON fields |
| --- | --- |
| `zero check --json` | Diagnostics with code, span, expected/actual details, help, repair metadata, `graph` identity for canonical source, `targetReadiness`, and `safetyFacts` for the selected target/emit kind. |
| `zero graph --json` | Modules, public symbols, capabilities, static facts, safety facts, helper use, and nested `programGraph`. |
| `zero graph dump --json` | The bare deterministic ProgramGraph with `moduleIdentity`, `graphHash`, validation, counts, nodes, and edges. Use `--out <program-graph-artifact>` to also write a derived graph artifact. |
| `zero graph import --json` | Source-to-ProgramGraph import with graph identity and validation. With `--out <program-graph-artifact>`, writes a derived graph artifact and reports `saved.path`. |
| `zero graph validate --json` | A derived ProgramGraph artifact readback check with `moduleIdentity`, `graphHash`, counts, validation state, and optional normalized artifact output path. |
| `zero graph view --json` | Canonical source text rendered from source or a ProgramGraph artifact with `moduleIdentity`, `graphHash`, and optional output path. |
| `zero graph source-map --json` | Graph node IDs mapped to source ranges with node hashes, symbol/type/effect IDs, and file hash facts. |
| `zero graph reconcile --json` | Identity decisions when edited source is compared with a prior graph, including ambiguous-match diagnostics and simple graph patch text when available. |
| `zero graph check --json` | Typecheck source or a ProgramGraph artifact through direct graph lowering with graph identity, target, `check.lowering: "direct-program-graph"`, target readiness, safety facts, and graph-mapped diagnostics. |
| `zero graph size --json` | Size, helper, runtime, profile, safety, and backend facts for a ProgramGraph artifact lowered through typed graph MIR, with graph identity. |
| `zero graph build --json` | Build a ProgramGraph artifact through typed graph MIR when supported, including graph identity, selected `emit` kind, target, artifact path and size, safety facts, compiler cache facts, and graph-aware incremental invalidation. |
| `zero graph patch --json` | Checked graph edits with graph-hash preconditions, per-operation node/field results, the changed graph hash, and the saved source or artifact path. |
| `zero graph roundtrip --json` | Source or ProgramGraph artifact stability through direct graph lowering with `semanticStable`, lowering mode, original and roundtripped graph hashes, raw counts, normalized semantic counts, and optional ProgramGraph output. |
| `zero dev --json` | A watch plan for changed source, manifest, package-lock, and generated-binding inputs. |
| `zero dev --json --trace` | Adds phase timing, cache hit/miss facts, diagnostics passthrough, and `interfaceFingerprints`. |
| `zero time --json` | Compiler phase timing plus `interfaceFingerprints` and incremental invalidation facts. |
| `zero build --json` | Artifact path, size, selected `toolchain`, target triple, linker flavor, sysroot status, `graph` identity, `safetyFacts`, and runtime provider facts when a helper such as hosted HTTP is linked. |
| `zero size --json` | `graph` identity, `profileSemantics`, `profileCatalog`, `profileBudget`, `safetyFacts`, `backendProfile`, `backendComparison`, `sizeBreakdown`, `retentionReasons`, and `optimizationHints`. |
| `zero ship --json` | A release preview with artifact names, hashes, graph identity, safety facts, a checksum file, debug-symbol metadata, size report, and SBOM placeholder. |
| `zero test --json` | Graph identity for canonical source, test discovery mode, selected fixtures, result counts, output, and per-test locations/failures. |
| `zero doctor --json` | Host checks plus `targetToolchains`, the per-target readiness matrix. |

Canonical `.0` source commands report a top-level `graph` object with
`artifact`, `canonicalSource`, `moduleIdentity`, `graphHash`, and `lowering`.
Their compiler cache and incremental invalidation facts use
`sourceKind: "program-graph"` and include the graph input that keyed the
compile. Derived ProgramGraph artifact commands report the same identity fields
for the artifact being inspected or built.

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

`.0` files are source text. ProgramGraph commands that write graph artifacts
must use a non-source output path, such as `.zero/out/app.program-graph`.
Agents can inspect source through ProgramGraph commands and can patch canonical
`.0` source through `zero graph patch`. ProgramGraph artifacts remain optional
debug and interchange files.

## ProgramGraph Patches

`zero graph patch` applies checked edits to a graph. When the input is
canonical `.0` source without comments, the command rewrites that source after
lowering, formatting, re-parsing, and semantic graph comparison succeeds. For
small edits, pass one or more operations inline:

```sh
zero graph patch \
  examples/hello.0 \
  --expect-graph-hash graph:a7f7e6899a73f3b4 \
  --op 'set node="#expr_653eeb6e" field="value" expect="hello from zero\n" value="hello patched\n"'
```

For larger edits, patch files are line-oriented text:

```text
zero-program-graph-patch v1
expect graphHash "graph:a7f7e6899a73f3b4"
set node="#expr_653eeb6e" field="value" expect="hello from zero\n" value="hello patched\n"
insert node="#patch001" kind="Literal" parent="#expr_c403020c" edge="arg" order="1" type="String" value="again\n"
rename node="#decl_ad8d9028" expect="main" value="start"
delete node="#patch001"
```

Use `--patch-text <text>` when a tool already has a complete patch document in
memory and should not create a temporary file.

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
| LLVM IR | `zero build --emit llvm-ir --backend llvm --target linux-musl-x64 <input>` |
| LLVM host executable | `zero build --backend llvm --emit exe --target host <input>` |

Removed backend flags report `BLD003`. Use direct emitters; the removed C
backend is not a compatibility path.

`direct` is the default backend family. `llvm` is an explicit backend family.
Use `--backend llvm --emit llvm-ir` to write a `.ll` artifact. On a supported
host with `clang`, `zero build --backend llvm --emit exe` and
`zero run --backend llvm` compile that IR into a native executable through an
external LLVM toolchain plan. Native LLVM object output and unsupported targets
report `BLD004` with `backendBlocker.backend: "llvm"` and do not fall back to
direct emitters. If the LLVM artifact references Zero runtime helpers, the JSON
build report lists the required runtime object in `objectBackend`.
`zero size --json --backend llvm` reports LLVM size/profile metadata, including
target triple, optimization level, retained runtime/helper facts, toolchain
readiness, and direct-vs-LLVM comparison rows without writing a native artifact.

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
zero check [--json] [--target <target>] [--emit exe|obj|llvm-ir] [--backend direct|llvm|<direct-emitter>] <input>
zero dev [--json] [--trace] [--target <target>] <input>
zero run [--backend direct|llvm|<direct-emitter>] [--target <target>] [--profile dev|release] [--out <file>] <input> [-- args...]
zero build [--emit exe|obj|llvm-ir] [--backend direct|llvm|<direct-emitter>] [--target <target>] [--profile dev|release] [--out <file>] <input>
zero ship [--json] [--target <target>] [--profile release-small|tiny|audit] [--out <file>] <input>
zero test [--json] [--filter <name>] [--target <target>] [--cc <path>] [--out <file>] <input>
zero fmt [--check] <input>
zero graph [dump|import|inspect|validate|view|source-map|reconcile|check|size|build|run|test|patch|roundtrip] [--json] [--target <target>] <input> [patch]
zero graph [dump|import|validate|roundtrip] [--json] --out <program-graph-artifact> <input>
zero graph view [--json] [--out <file.0>] <program-graph-or-source>
zero graph source-map --json <program-graph-or-source>
zero graph reconcile [--json] <base-program-graph-or-source> --source <edited-file.0|project|zero.json>
zero graph size [--json] [--target <target>] --out <artifact> <program-graph-or-package>
zero graph patch [--json] [--out <program-graph-artifact>] <program-graph-or-source> (<patch-file>|--op <operation>)
zero graph build [--json] [--emit exe|obj|llvm-ir] [--backend direct|llvm|<direct-emitter>] [--target <target>] [--profile debug|dev|release-fast|release-small|tiny|audit] [--release <profile>] [--out <file>] <program-graph-or-package>
zero graph run [--target <host-target>] [--profile debug|dev|release-fast|release-small|tiny|audit] [--release <profile>] [--out <file>] <program-graph-or-package> [-- args...]
zero graph test [--json] [--filter <name>] [--target <target>] <program-graph-or-package>
zero doc [--json] [--target <target>] <input>
zero size [--json] [--backend direct|llvm|<direct-emitter>] [--target <target>] [--out <artifact>] <input>
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
