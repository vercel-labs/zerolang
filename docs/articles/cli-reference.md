## CLI Reference

`zero` checks, formats, runs, tests, builds, inspects, and repairs Zero programs.

Most commands accept the same input forms:

| Input | Meaning |
| --- | --- |
| `file.graph` | A binary or text graph store/artifact used directly by compiler commands. |
| `file.0` | Human-readable Zero projection text for review, formatting, and human import/export workflows. It is not a compiler input; pass the package or `.graph` store instead. |
| `project/` | A package directory containing `zero.toml` or `zero.json`; graph-first packages compile from `zero.graph`. |
| `zero.toml` | A TOML package manifest. Takes precedence for directory inputs when both manifests exist. |
| `zero.json` | Compatibility JSON package manifest. Prefer `zero.toml` for new packages. |

## Daily Commands

| Command | Use it for |
| --- | --- |
| `zero check <graph-input>` | Validate graph-backed input, typecheck, and report diagnostics. |
| `zero patch <graph-input>` | Apply checked graph edits. |
| `zero run <graph-input>` | Build and run a host executable with the selected backend. |
| `zero test <graph-input>` | Run inline `test` blocks from graph-backed input. |
| `zero fmt <source-input>` | Print formatted source projections. Add `--check` in CI. |
| `zero build <graph-input>` | Emit an executable or object file. |
| `zero ship <graph-input>` | Produce a release preview with checksums and metadata. |
| `zero inspect <graph-input>` | Inspect modules, symbols, capabilities, helper use, and ProgramGraph facts. |
| `zero size <graph-input>` | Explain artifact size, retained helpers, and profile budgets. |
| `zero doc <graph-input>` | Emit public API documentation facts. |
| `zero fix --plan --json <graph-input>` | Ask for a typed repair plan from graph-backed input. |
| `zero doctor` | Check host and target readiness. |

Copyable examples:

```sh
zero check examples/hello.graph
zero run examples/add.graph
zero test conformance/native/pass/test-blocks.graph
zero build --emit exe --target linux-musl-x64 examples/add.graph --out .zero/out/add
zero init .zero/out/graph-hello
zero patch .zero/out/graph-hello --op 'addMain' --op 'addCheckWrite fn="main" text="hello from graph\n"'
zero query .zero/out/graph-hello
zero query --fn main .zero/out/graph-hello
zero query --find write .zero/out/graph-hello
zero query --calls std .zero/out/graph-hello
zero query --refs add .zero/out/graph-hello
zero query --node '#expr_2cad38f9' .zero/out/graph-hello
zero run .zero/out/graph-hello
zero export .zero/out/graph-hello
zero inspect examples/systems-package
zero query examples/hello.graph
zero dump examples/hello.graph
zero inspect examples/hello.graph
zero view examples/hello.graph
zero source-map examples/hello.graph
zero status .
zero patch examples/hello.graph --expect-graph-hash graph:a7f7e6899a73f3b4 --op 'set node="#expr_653eeb6e" field="value" expect="hello from zero\n" value="hello patched\n"'
zero patch --op help
zero roundtrip examples/hello.graph
zero size examples/point.graph
zero ship --target linux-musl-x64 examples/hello.graph --out .zero/ship/hello
zero doctor
```

## Run

`zero run` builds a host executable, runs it, passes through program
stdout/stderr, and exits with the program status. Direct output is the default;
pass `--backend llvm` for explicit LLVM host execution when `clang` is ready.

Pass program arguments after `--`:

```sh
zero run examples/cli-file.graph -- input.txt
```

You usually do not need a `.program-graph` file to debug, inspect, diagnose,
run, or test a graph-first program. Use `zero query`, `zero view`, `zero
check`, `zero run`, and `zero test` directly on the package. Create a
standalone `.program-graph` only when another tool needs a file to carry the
graph outside the repository store:

```sh
zero dump --out .zero/out/hello.program-graph examples/hello.graph
zero check .zero/out/hello.program-graph
zero view .zero/out/hello.program-graph
```

## JSON Output

Normal command output is designed to be readable by agents. Use `--json` when
another tool needs stable fields.

| Command | Useful JSON fields |
| --- | --- |
| `zero check --json` | Diagnostics with code, span, expected/actual details, help, repair metadata, graph identity for package and graph artifact inputs, target readiness, direct graph lowering facts, and safety facts for the selected target/emit kind. |
| `zero inspect --json` | Modules, public symbols, capabilities, static facts, safety facts, helper use, and nested `programGraph`. |
| `zero init --json` | Create a graph-first package with a manifest and `zero.graph`, no materialized `.0` source projection, and next-step graph patch commands. |
| `zero query --json` | Compact graph-first facts for tools: input kind, graph identity, selector metadata, module/function/body summaries, resolver-backed call/reference facts, matched node handles, selected-node neighborhoods, statement IDs, and common patch operations. |
| `zero dump --json` | The bare deterministic ProgramGraph with `moduleIdentity`, `graphHash`, validation, counts, nodes, and edges. Use `--out <program-graph-artifact>` to also write a derived graph artifact. |
| `zero import --json` | Human projection to graph import with graph identity and validation. With `--out <program-graph-artifact>`, writes a derived graph artifact and reports `saved.path`. |
| `zero validate --json` | A derived ProgramGraph artifact readback check with `moduleIdentity`, `graphHash`, counts, validation state, and optional normalized artifact output path. |
| `zero view --json` | Canonical source projection rendered from a graph input with `moduleIdentity`, `graphHash`, and optional output path. |
| `zero source-map --json` | Graph node IDs mapped to source ranges with node hashes, symbol/type/effect IDs, and file hash facts. |
| `zero reconcile --json` | Identity decisions when edited source is compared with a prior graph, including ambiguous-match diagnostics and simple graph patch text when available. |
| `zero status --json` | Repository graph projection facts, the expected `zero.graph` path, no-write status, store validity, and whether checked-in projections are current. |
| `zero verify-projection --json` | A no-write projection drift check that compares a valid repository graph store with checked-in `.0` projection bytes and reports import/export repair choices on drift. |
| `zero merge --json` | Three-way repository graph store merge with base/left/right stores, durable-node conflict diagnostics, changed-path reporting, storage facts, and scale counts. |
| `zero size --json` | Size, helper, runtime, profile, safety, and backend facts for a ProgramGraph artifact lowered through typed graph MIR, with graph identity. |
| `zero build --json` | Build a ProgramGraph artifact through typed graph MIR when supported, including graph identity, selected `emit` kind, target, artifact path and size, safety facts, compiler cache facts, and graph-aware incremental invalidation. |
| `zero patch --json` | Checked graph edits with graph-hash preconditions, per-operation node/field results, the changed graph hash, and the saved source or artifact path. |
| `zero roundtrip --json` | Graph artifact stability through direct graph lowering with `semanticStable`, lowering mode, original and roundtripped graph hashes, raw counts, normalized semantic counts, and optional ProgramGraph output. |
| `zero dev --json` | A watch plan for changed source, manifest, package-lock, and generated-binding inputs. |
| `zero dev --json --trace` | Adds phase timing, cache hit/miss facts, diagnostics passthrough, and `interfaceFingerprints`. |
| `zero time --json` | Compiler phase timing plus `interfaceFingerprints` and incremental invalidation facts. |
| `zero build --json` | Artifact path, size, selected `toolchain`, target triple, linker flavor, sysroot status, `graph` identity, `safetyFacts`, and runtime provider facts when a helper such as hosted HTTP is linked. |
| `zero size --json` | `graph` identity, `profileSemantics`, `profileCatalog`, `profileBudget`, `safetyFacts`, `backendProfile`, `backendComparison`, `sizeBreakdown`, `retentionReasons`, and `optimizationHints`. |
| `zero ship --json` | A release preview with artifact names, hashes, graph identity, safety facts, a checksum file, debug-symbol metadata, size report, and SBOM placeholder. |
| `zero test --json` | Graph identity, test discovery mode, selected fixtures, result counts, output, and per-test locations/failures. |
| `zero doctor --json` | Host checks plus `targetToolchains`, the per-target readiness matrix. |

Graph-backed compiler commands for `zero check`, `zero build`, `zero size`,
`zero ship`, `zero mem`, and `zero test` report a top-level `graph` object
with `artifact`, `canonicalSource`, `moduleIdentity`, `graphHash`, and
`lowering`. Their compiler cache and incremental invalidation facts use
`sourceKind: "program-graph"` and include the graph input that keyed the
compile. Planning and introspection commands such as `zero dev`, `zero time`,
`zero doc`, and `zero abi` report graph cache facts for graph-backed inputs.
Derived ProgramGraph artifact commands report the same identity fields for the
artifact being inspected or built.

Repository graph build/run/test/size/ship/mem commands and standalone
`.program-graph` build/run/size commands can also report
`lowering: "mapped-final-mir"`. That means the graph input was lowered to
compact final MIR, written under
`.zero/cache/native/mir-*.zmir`, memory-mapped, verified against the compiler
version, graph hash, target, emit kind, and backend request, then passed to
codegen. For warm repository `zero.graph` build/run commands and warm
standalone `.program-graph` build/run commands, a mapped MIR hit is the
immediate codegen input: the compiler skips graph-to-MIR lowering and checked
Program reconstruction. Graph-backed `zero size` derives helper and capability
summaries from graph/IR facts instead of reconstructing checked Program facts. The
`compilerCaches` array includes a `mappedFinalMir` row with the cache path,
byte length, whether the cache was reused or written by this command, whether
codegen borrowed stable storage from the mapped file, and the
`codegenImmediate`/`programReconstructed` facts for the command.
`zero.graph` remains the authoring store; `.program-graph` and `.zmir` files
are derived compiler artifacts.

`zero check --json` and `zero inspect --json` also include `compileTime`.
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

`.0` files are human-readable source text. For graph-first packages,
`zero.graph` is the agent write surface and normal compiler input; `.0` files
are projections for human review and direct human edits. ProgramGraph commands
that write derived graph artifacts must use a non-source output path, such as
`.zero/out/app.program-graph`. ProgramGraph artifacts remain optional debug and
interchange files.

`zero status`, `zero verify-projection`, `zero import`, and `zero export` define the
repository graph projection surface.
`zero init --manifest toml <project>` creates a graph-first package with
`zero.toml` and `zero.graph`. It does not materialize `.0` files. Agents can
patch the package with
`zero patch <project> --op ...`; from inside a graph-first package,
`zero patch --op ...` defaults to the current directory. Then normal
`zero check`, `zero run`, and `zero test` run against the graph store.
`zero import` writes a deterministic `zero.graph` repository
store from current `.0` source, preserves existing graph node handles where the
source edit is unambiguous, and stores exact checked-in source projection bytes
for tracked local files. Ambiguous identity changes fail instead of guessing.
Binary is the default `zero.graph` encoding. `zero init`, `zero patch`,
`zero import`, and `zero merge` write a binary repository graph
store unless an existing text store is being preserved or `--format text` is
explicitly requested. Binary stores are decoded as typed graph tables instead
of parsed as a text wrapper. `zero dump`, `zero import`, `zero validate`,
`zero patch --out`, and `zero roundtrip --out` still write readable text
artifacts by default, but can write binary graph artifacts with `--format
binary`. Reads auto-detect both encodings, plain package writes preserve an
existing text or binary store, and `zero status` reports the active store
format. Stdlib `std/*.graph` stores are binary graph stores used by the
compiler path; sibling `std/*.0` files are human-readable projections, not the
stdlib compile source.
`zero export` rewrites stale `.0` source projections from that store, and
`zero verify-projection` checks the store against checked-in source projection
bytes without writing files. Normal compiler commands validate and compile from
the graph store, including target and package metadata, so graph packages can
still be checked, built, run, tested, sized, shipped, and inspected when `.0`
projections are missing. Commands report source projection state and do not
rewrite `.0` files; run `zero verify-projection` when checked-in projection
drift must fail the workflow. Packages without `zero.graph` are missing their
compiler input.
`zero merge --base <base-zero.graph> --left <left-zero.graph> --right
<right-zero.graph> <project|zero.toml|zero.json|file.0>` combines independent
repository graph store edits by durable node ID and node hash, writes the
target `zero.graph` on success, and reports conflicts by graph node, source
projection, semantic object, and field.
It does not rewrite `.0` projections; run `zero export` after a
successful merge when the checked-in source projection should be refreshed.
In this repository, `pnpm run repository-graph:check` verifies checked-in
`zero.graph` stores for CI with the pinned `linux-musl-x64` graph target.

## ProgramGraph Patches

`zero patch` applies checked edits to a graph. When the input is a repository
graph package, the command writes `zero.graph` after loading
the store, applying operations, validating graph readiness, and rechecking
compiler input. For small edits, pass one or more operations inline:

```sh
cd .zero/out/graph-hello
zero patch \
  --op 'addMain' \
  --op 'addCheckWrite fn="main" text="hello from graph\n"'
zero check .
zero run .
```

Preview a repository graph patch without writing:

```sh
zero patch --check-only <package> /tmp/body.patch
zero patch --dry-run --json <package> /tmp/body.patch
```

Agents can compose larger functions without writing node tables or `.0` source:

```sh
zero patch \
  --op 'addFunction name="add_twice" ret="u32"' \
  --op 'addParam fn="add_twice" name="x" type="u32"' \
  --op 'addParam fn="add_twice" name="y" type="u32"' \
  --op 'addLetBinary fn="add_twice" name="first" type="u32" operator="+" left="x" right="y"' \
  --op 'addLetBinary fn="add_twice" name="total" type="u32" operator="+" left="first" right="y"' \
  --op 'addReturnValue fn="add_twice" value="total" type="u32"'
zero check .
```

Direct example files should be patched through their `.graph` sidecar or a
package graph store. Keep `.0` edits for humans; after a reviewed human edit,
run `zero import` to rebuild the graph store:

```sh
zero patch \
  --out examples/hello.graph \
  examples/hello.graph \
  --expect-graph-hash graph:a7f7e6899a73f3b4 \
  --op 'set node="#expr_653eeb6e" field="value" expect="hello from zero\n" value="hello patched\n"'
zero view --out examples/hello.0 examples/hello.graph
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

For common body edits, patch files can use row syntax inside
`replaceFunctionBody` for a whole function or `replaceBlockBody` for a selected
`Block` node:

```text
zero-program-graph-patch v1
expect graphHash "graph:a7f7e6899a73f3b4"
replaceFunctionBody main
  let name Maybe<String> = std.args.get 1
  if name.has
    check world.out.write "hello "
    check world.out.write name.value
    check world.out.write "\n"
  else
    check world.out.write "hello anonymous\n"
end
```

To replace only one branch or nested block, query a block handle and patch that
block body:

```sh
zero query --find Block .
```

```text
zero-program-graph-patch v1
expect graphHash "graph:a7f7e6899a73f3b4"
replaceBlockBody #block_32cefdd9
  check world.out.write "name: "
  check world.out.write name.value
  check world.out.write "\n"
end
```

Use `--patch-text <text>` when a tool already has a complete patch document in
memory and should not create a temporary file.

The header is required. `expect graphHash` is optional but recommended; it
rejects edits against a different artifact. `set` requires `node`, `field`, and
`value`; `expect` is optional and rejects the operation when the current field
value differs.

Supported operations are `set`, `insert`, `insertEdge`, `replace`, `delete`,
`rename`, `addFunction`, `addMain`, `addParam`, `addReturnBinary`,
`addLetLiteral`, `addLetBinary`, `addReturnValue`, `addCheckWrite`,
`addCheckWriteValue`, `addTest`, `replaceFunctionBody`, and
`replaceBlockBody`. `insert` creates a node and
connects it to a parent node with an ordered node edge; missing `order` defaults
to `0`. `insertEdge`
connects existing graph facts across `node`, `symbol`, `type`, or `effect`
target domains. `replace` updates a node in place and can require the current
node hash through `expect`. `delete` removes an owned subtree and rejects
external references into that subtree. `rename` updates a node name with an
optional current-name precondition. The `add*` operations create common
function, parameter, local value, return, output, and test structures without
requiring an agent to hand-author graph node IDs. Use row syntax for
workflow-specific behavior such as CLI argument parsing rather than relying on
program-specific patch shortcuts. `replaceFunctionBody` replaces a function body
from compact row syntax while still writing the repository graph.
`replaceBlockBody` uses the same row syntax to replace only the statements owned
by a selected `Block` node, such as an `if` branch.

Run `zero patch --op help` to list supported operation shapes without
loading or writing a graph.

Editable scalar fields are `name`, `type`, `value`, `public`, `mutable`,
`static`, `fallible`, and `exportC`. Boolean fields accept only `true` or
`false`. `name` values must be identifier paths or supported operator tokens.
`type` values must be valid Zero type syntax. Strings support `\\`, `\"`,
`\n`, `\r`, `\t`, and `\u00XX` escapes for non-NUL bytes. NUL bytes are not
valid ProgramGraph patch text.

## Build Outputs

| Emit mode | Command |
| --- | --- |
| Native executable | `zero build --emit exe --target linux-musl-x64 <graph-input>` |
| Native object | `zero build --emit obj --target linux-musl-x64 <graph-input>` |
| LLVM IR | `zero build --emit llvm-ir --backend llvm --target linux-musl-x64 <graph-input>` |
| LLVM host executable | `zero build --backend llvm --emit exe --target host <graph-input>` |

Removed backend flags report `BLD003`. Use direct emitters; the removed C
backend is not a compatibility path.

`direct` is the default backend family. `llvm` is an explicit experimental
backend family. It is not default eligible, release eligible, or accepted by
`zero ship`; direct emitters remain the supported release path. Use
`--backend llvm --emit llvm-ir` to write a `.ll` artifact. On a supported host
with `clang`, `zero build --backend llvm --emit exe` and
`zero run --backend llvm` compile that IR into a native executable through an
external LLVM toolchain plan. LLVM lowering currently supports scalar code,
direct calls, branches, loops, primitive fixed arrays, byte views, readonly
strings, and primitive `std.mem` helpers. Native LLVM object output,
unsupported targets, unsupported MIR constructs, and `zero ship --backend llvm`
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
zero check [--json] [--target <target>] [--emit exe|obj|llvm-ir] [--backend direct|llvm|<direct-emitter>] <graph-input>
zero dev [--json] [--trace] [--target <target>] <graph-input>
zero run [--backend direct|llvm|<direct-emitter>] [--target <target>] [--profile dev|release] [--out <file>] <graph-input> [-- args...]
zero build [--emit exe|obj|llvm-ir] [--backend direct|llvm|<direct-emitter>] [--target <target>] [--profile dev|release] [--out <file>] <graph-input>
zero ship [--json] [--target <target>] [--profile release-small|tiny|audit] [--out <file>] <graph-input>
zero test [--json] [--filter <name>] [--target <target>] [--cc <path>] [--out <file>] <graph-input>
zero fmt [--check] <source-input>
zero init [--json] [--manifest toml|json] <project-path>
zero query [--json] [--fn <name>] [--find <text>] [--refs <name>] [--calls <name>] [--node <id>] <graph-input>
zero dump|validate|roundtrip [--json] [--format text|binary] [--out <program-graph-artifact>] <graph-input>
zero view [--json] [--out <file.0>] <graph-input>
zero source-map [--json] <graph-input>
zero reconcile [--json] <base-graph-input> --source <edited-file.0|project|zero.toml|zero.json>
zero status|verify-projection [--json] <project|zero.toml|zero.json|file.0>
zero import [--json] [--format text|binary] [--out <program-graph-artifact>] <project|zero.toml|zero.json|file.0>
zero export [--json] <project|zero.toml|zero.json|file.0>
zero merge --base <base-zero.graph> --left <left-zero.graph> --right <right-zero.graph> [--json] <project|zero.toml|zero.json|file.0>
zero size [--json] [--target <target>] --out <artifact> <graph-input>
zero patch [--json] [--check-only|--dry-run] [--out <program-graph-artifact>] [<graph-input>] (<patch-file>|--op <operation>)
zero build [--json] [--emit exe|obj|llvm-ir] [--backend direct|llvm|<direct-emitter>] [--target <target>] [--profile debug|dev|release-fast|release-small|tiny|audit] [--release <profile>] [--out <file>] <graph-input>
zero run [--target <host-target>] [--profile debug|dev|release-fast|release-small|tiny|audit] [--release <profile>] [--out <file>] <graph-input> [-- args...]
zero test [--json] [--filter <name>] [--target <target>] <graph-input>
zero doc [--json] [--target <target>] <graph-input>
zero size [--json] [--backend direct|llvm|<direct-emitter>] [--target <target>] [--out <artifact>] <graph-input>
zero explain [--json] <diagnostic-code>
zero fix --plan --json [--target <target>] <graph-input>
zero targets
zero clean [--all]
zero mem [--json] [--target <target>] <graph-input>
zero time --json [--target <target>] <graph-input>
zero abi check|dump [--json] [--target <target>] <graph-input>
zero tokens --json <source-input>
zero parse --json <source-input>
```
