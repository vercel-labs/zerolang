## CLI Reference

`zero` checks, formats, runs, tests, builds, inspects, and repairs Zero programs.

Most commands accept the same input forms:

| Input | Meaning |
| --- | --- |
| `file.0` | Human-readable Zero source text. In graph-first packages this is a projection, not the normal agent write surface. |
| `project/` | A package directory containing `zero.json`; graph-first packages compile from `zero.graph`. |
| `zero.json` | A package manifest. |

## Daily Commands

| Command | Use it for |
| --- | --- |
| `zero check <input>` | Parse, typecheck, and report diagnostics. |
| `zero patch <input>` | Apply checked graph edits. |
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
zero graph init .zero/out/graph-hello
zero patch .zero/out/graph-hello --op 'addMain' --op 'addCheckWrite fn="main" text="hello from graph\n"'
zero graph query .zero/out/graph-hello
zero graph query --fn main .zero/out/graph-hello
zero graph query --find write .zero/out/graph-hello
zero graph query --calls std .zero/out/graph-hello
zero graph query --refs add .zero/out/graph-hello
zero graph query --node '#expr_2cad38f9' .zero/out/graph-hello
zero run .zero/out/graph-hello
zero graph sync --from-graph .zero/out/graph-hello
zero graph examples/systems-package
zero graph query examples/hello.0
zero graph dump examples/hello.0
zero graph dump --out .zero/out/hello.program-graph examples/hello.0
zero graph import --out .zero/out/hello.program-graph examples/hello.0
zero graph inspect examples/hello.0
zero graph validate .zero/out/hello.program-graph
zero graph view examples/hello.0
zero graph view --out .zero/out/hello.view.0 .zero/out/hello.program-graph
zero graph source-map examples/hello.0
zero graph reconcile .zero/out/hello.program-graph --source examples/hello.0
zero graph status .
zero check .zero/out/hello.program-graph
zero graph size .zero/out/hello.program-graph
zero graph build --emit obj --target linux-musl-x64 --out .zero/out/hello.o .zero/out/hello.program-graph
zero graph run .zero/out/hello.program-graph
zero graph test .zero/out/hello.program-graph
zero patch examples/hello.0 --expect-graph-hash graph:a7f7e6899a73f3b4 --op 'set node="#expr_653eeb6e" field="value" expect="hello from zero\n" value="hello patched\n"'
zero patch --op help
zero graph roundtrip examples/hello.0
zero graph roundtrip .zero/out/hello.program-graph
zero size examples/point.0
zero ship --target linux-musl-x64 examples/hello.0 --out .zero/ship/hello
zero doctor
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
| `zero check --json` | Diagnostics with code, span, expected/actual details, help, repair metadata, graph identity for source/package/artifact inputs, target readiness, direct graph lowering facts for ProgramGraph inputs, and safety facts for the selected target/emit kind. |
| `zero graph --json` | Modules, public symbols, capabilities, static facts, safety facts, helper use, and nested `programGraph`. |
| `zero graph init --json` | Create a graph-first package with `zero.json` and `zero.graph`, no materialized `.0` source projection, and next-step graph patch commands. |
| `zero graph query --json` | Compact graph-first facts for tools: input kind, graph identity, selector metadata, module/function/body summaries, resolver-backed call/reference facts, matched node handles, selected-node neighborhoods, statement IDs, and common patch operations. |
| `zero graph dump --json` | The bare deterministic ProgramGraph with `moduleIdentity`, `graphHash`, validation, counts, nodes, and edges. Use `--out <program-graph-artifact>` to also write a derived graph artifact. |
| `zero graph import --json` | Source-to-ProgramGraph import with graph identity and validation. With `--out <program-graph-artifact>`, writes a derived graph artifact and reports `saved.path`. |
| `zero graph validate --json` | A derived ProgramGraph artifact readback check with `moduleIdentity`, `graphHash`, counts, validation state, and optional normalized artifact output path. |
| `zero graph view --json` | Canonical source text rendered from source or a ProgramGraph artifact with `moduleIdentity`, `graphHash`, and optional output path. |
| `zero graph source-map --json` | Graph node IDs mapped to source ranges with node hashes, symbol/type/effect IDs, and file hash facts. |
| `zero graph reconcile --json` | Identity decisions when edited source is compared with a prior graph, including ambiguous-match diagnostics and simple graph patch text when available. |
| `zero graph status --json` | Repository graph sync facts, the expected `zero.graph` path, no-write status, store validity, and whether graph/source sync is enabled. |
| `zero graph verify-sync --json` | A no-write graph/source sync check that compares a valid repository graph store with the current source graph and reports repair commands on drift. |
| `zero graph merge --json` | Three-way repository graph store merge with base/left/right stores, durable-node conflict diagnostics, changed-path reporting, storage facts, and scale counts. |
| `zero graph size --json` | Size, helper, runtime, profile, safety, and backend facts for a ProgramGraph artifact lowered through typed graph MIR, with graph identity. |
| `zero graph build --json` | Build a ProgramGraph artifact through typed graph MIR when supported, including graph identity, selected `emit` kind, target, artifact path and size, safety facts, compiler cache facts, and graph-aware incremental invalidation. |
| `zero patch --json` | Checked graph edits with graph-hash preconditions, per-operation node/field results, the changed graph hash, and the saved source or artifact path. |
| `zero graph roundtrip --json` | Source or ProgramGraph artifact stability through direct graph lowering with `semanticStable`, lowering mode, original and roundtripped graph hashes, raw counts, normalized semantic counts, and optional ProgramGraph output. |
| `zero dev --json` | A watch plan for changed source, manifest, package-lock, and generated-binding inputs. |
| `zero dev --json --trace` | Adds phase timing, cache hit/miss facts, diagnostics passthrough, and `interfaceFingerprints`. |
| `zero time --json` | Compiler phase timing plus `interfaceFingerprints` and incremental invalidation facts. |
| `zero build --json` | Artifact path, size, selected `toolchain`, target triple, linker flavor, sysroot status, `graph` identity, `safetyFacts`, and runtime provider facts when a helper such as hosted HTTP is linked. |
| `zero size --json` | `graph` identity, `profileSemantics`, `profileCatalog`, `profileBudget`, `safetyFacts`, `backendProfile`, `backendComparison`, `sizeBreakdown`, `retentionReasons`, and `optimizationHints`. |
| `zero ship --json` | A release preview with artifact names, hashes, graph identity, safety facts, a checksum file, debug-symbol metadata, size report, and SBOM placeholder. |
| `zero test --json` | Graph identity for canonical source, test discovery mode, selected fixtures, result counts, output, and per-test locations/failures. |
| `zero doctor --json` | Host checks plus `targetToolchains`, the per-target readiness matrix. |

Canonical `.0` source JSON for `zero check`, `zero build`, `zero size`,
`zero ship`, `zero mem`, and `zero test` reports a top-level `graph` object
with `artifact`, `canonicalSource`, `moduleIdentity`, `graphHash`, and
`lowering`. Their compiler cache and incremental invalidation facts use
`sourceKind: "program-graph"` and include the graph input that keyed the
compile. Planning and introspection commands such as `zero dev`, `zero time`,
`zero doc`, and `zero abi` continue to report canonical source cache facts.
Derived ProgramGraph artifact commands report the same identity fields for the
artifact being inspected or built.

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

`.0` files are human-readable source text. For graph-first packages,
`zero.graph` is the agent write surface and normal compiler input; `.0` files
are projections for human review and direct human edits. ProgramGraph commands
that write derived graph artifacts must use a non-source output path, such as
`.zero/out/app.program-graph`. ProgramGraph artifacts remain optional debug and
interchange files.

`zero graph status`, `zero graph verify-sync`, and `zero graph sync
--from-source|--from-graph` define the repository graph sync surface.
`zero graph init <project>` creates a graph-first package with
`repositoryGraph.compilerInput: true`, `zero.json`, and `zero.graph`; it does
not materialize `.0` files. Agents can patch the package with
`zero patch <project> --op ...`; from inside a graph-first package,
`zero patch --op ...` defaults to the current directory. Then normal
`zero check`, `zero run`, and `zero test` run against the graph store.
`zero graph sync --from-source` writes a deterministic `zero.graph` repository
store from current `.0` source, preserves existing graph node handles where the
source edit is unambiguous, and stores exact checked-in source projection bytes
for tracked local files. Ambiguous identity changes fail instead of guessing.
`zero graph sync --from-graph` rewrites stale `.0` source projections from that
store, and `zero graph verify-sync` checks the store against the current source
graph and source projection without writing files. Packages can opt normal
check, build, run, test, size, ship, and mem commands into the checked-in store
with `repositoryGraph.compilerInput: true` in `zero.json`. Normal compiler
commands validate and compile from the graph store, including target and package
metadata, so source-free graph packages can still be checked, built, run,
tested, sized, shipped, and inspected. Commands report source projection state
and do not rewrite `.0` files; run `zero graph verify-sync` when graph and
source projection drift must fail the workflow. Packages without that marker
still use checked-in `.0` source text as their compiler input.
`zero graph merge --base <base-zero.graph> --left <left-zero.graph> --right
<right-zero.graph> <input>` combines independent repository graph store edits by
durable node ID and node hash, writes the target `zero.graph` on success, and
reports conflicts by graph node, source projection, semantic object, and field.
It does not rewrite `.0` projections; run `zero graph sync --from-graph` after a
successful merge when the checked-in source projection should be refreshed.
In this repository, `pnpm run repository-graph:check` verifies checked-in
`zero.graph` stores for CI with the pinned `linux-musl-x64` graph target.

## ProgramGraph Patches

`zero patch` applies checked edits to a graph. When the input is an
opted-in repository graph package, the command writes `zero.graph` after loading
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

It can also patch canonical `.0` source without comments; that path rewrites
the source after lowering, formatting, re-parsing, and semantic graph
comparison succeeds:

```sh
zero patch \
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

For common function-body edits, patch files can use row syntax inside
`replaceFunctionBody`:

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

Use `--patch-text <text>` when a tool already has a complete patch document in
memory and should not create a temporary file.

The header is required. `expect graphHash` is optional but recommended; it
rejects edits against a different artifact. `set` requires `node`, `field`, and
`value`; `expect` is optional and rejects the operation when the current field
value differs.

Supported operations are `set`, `insert`, `insertEdge`, `replace`, `delete`,
`rename`, `addFunction`, `addMain`, `addParam`, `addReturnBinary`,
`addLetLiteral`, `addLetBinary`, `addReturnValue`, `addCheckWrite`,
`addCheckWriteValue`, `addTest`, `setMainArgsAddCli`, `setMainGreetingCli`, and
`replaceFunctionBody`. `insert` creates a node and connects it to a parent node
with an ordered node edge; missing `order` defaults to `0`. `insertEdge`
connects existing graph facts across `node`, `symbol`, `type`, or `effect`
target domains. `replace` updates a node in place and can require the current
node hash through `expect`. `delete` removes an owned subtree and rejects
external references into that subtree. `rename` updates a node name with an
optional current-name precondition. The `add*` operations create common
function, parameter, local value, return, output, and test structures without
requiring an agent to hand-author graph node IDs. `setMainArgsAddCli` replaces
`main` with a small hosted CLI that parses two `u32` arguments, calls the named
add function, and writes the formatted sum. `setMainGreetingCli` creates a
first-argument greeting CLI. `replaceFunctionBody` replaces a function body from
compact row syntax while still writing the repository graph.

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
| Native executable | `zero build --emit exe --target linux-musl-x64 <input>` |
| Native object | `zero build --emit obj --target linux-musl-x64 <input>` |
| LLVM IR | `zero build --emit llvm-ir --backend llvm --target linux-musl-x64 <input>` |
| LLVM host executable | `zero build --backend llvm --emit exe --target host <input>` |

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
zero check [--json] [--target <target>] [--emit exe|obj|llvm-ir] [--backend direct|llvm|<direct-emitter>] <input>
zero dev [--json] [--trace] [--target <target>] <input>
zero run [--backend direct|llvm|<direct-emitter>] [--target <target>] [--profile dev|release] [--out <file>] <input> [-- args...]
zero build [--emit exe|obj|llvm-ir] [--backend direct|llvm|<direct-emitter>] [--target <target>] [--profile dev|release] [--out <file>] <input>
zero ship [--json] [--target <target>] [--profile release-small|tiny|audit] [--out <file>] <input>
zero test [--json] [--filter <name>] [--target <target>] [--cc <path>] [--out <file>] <input>
zero fmt [--check] <input>
zero graph [init|query|dump|import|inspect|validate|view|source-map|reconcile|status|verify-sync|sync|merge|size|build|run|test|roundtrip] [--json] [--target <target>] <input>
zero graph init [--json] <project-path>
zero graph query [--json] [--fn <name>] [--find <text>] [--refs <name>] [--calls <name>] [--node <id>] <program-graph-or-source>
zero graph [dump|import|validate|roundtrip] [--json] --out <program-graph-artifact> <input>
zero graph view [--json] [--out <file.0>] <program-graph-or-source>
zero graph source-map [--json] <program-graph-or-source>
zero graph reconcile [--json] <base-program-graph-or-source> --source <edited-file.0|project|zero.json>
zero graph status|verify-sync [--json] <project|zero.json|file.0>
zero graph sync (--from-source|--from-graph) [--json] <project|zero.json|file.0>
zero graph merge --base <base-zero.graph> --left <left-zero.graph> --right <right-zero.graph> [--json] <project|zero.json|file.0>
zero graph size [--json] [--target <target>] --out <artifact> <program-graph-or-package>
zero patch [--json] [--check-only|--dry-run] [--out <program-graph-artifact>] [<program-graph-or-source>] (<patch-file>|--op <operation>)
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
