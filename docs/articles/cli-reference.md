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
zero graph import --out .zero/out/hello.graph examples/hello.0
zero graph inspect --json examples/hello.0
zero graph validate .zero/out/hello.program-graph
zero graph view .zero/out/hello.program-graph
zero graph view --out .zero/out/hello.view.0 .zero/out/hello.program-graph
zero graph check --json .zero/out/hello.program-graph
zero graph size --json .zero/out/hello.program-graph
zero graph build --json --emit obj --target linux-musl-x64 --out .zero/out/hello.o .zero/out/hello.program-graph
zero graph run .zero/out/hello.program-graph
zero graph test --json .zero/out/hello.program-graph
zero graph patch .zero/out/hello.view.0 --expect-graph-hash graph:f76987e99677f1b3 --op 'set node="#610c78bf" field="value" expect="hello from zero\n" value="hello patched\n"'
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
zero graph run .zero/out/cli-file.graph -- input.txt
```

## JSON Output

Use `--json` when another tool will read the result. Text output is for people.
Use `--zdn` (or `--format zdn`) when an AI agent will read the result — ZDN
preserves the same structured data as JSON in an agent-friendlier
indentation-based format.

Three output formats are available on every command that accepts `--json`:

| Flag | Format | Use for |
| --- | --- | --- |
| _(none)_ | **TEXT** | Human-readable terminal output (default) |
| `--json` | **JSON** | External tools, CI, editors |
| `--zdn` / `--format zdn` | **ZDN** | AI agents and LLMs |

| Command | Useful JSON fields |
| --- | --- |
| `zero check --json` | Diagnostics with code, span, expected/actual details, help, repair metadata, `targetReadiness`, and `safetyFacts` for the selected target/emit kind. |
| `zero graph --json` | Modules, public symbols, capabilities, static facts, safety facts, helper use, and nested `programGraph`. |
| `zero graph dump --json` | The bare deterministic ProgramGraph with `moduleIdentity`, `graphHash`, validation, counts, nodes, and edges. Use `--out <program-graph-artifact>` to also write a derived graph artifact. |
| `zero graph import --json` | Source-to-ProgramGraph import with graph identity and validation. With `--out <program-graph-artifact>`, writes a derived graph artifact and reports `saved.path`. |
| `zero graph validate --json` | A derived ProgramGraph artifact readback check with `moduleIdentity`, `graphHash`, counts, validation state, and optional normalized artifact output path. |
| `zero graph view --json` | Canonical source text rendered from source or a ProgramGraph artifact with `moduleIdentity`, `graphHash`, and optional output path. |
| `zero graph check --json` | Typecheck source or a ProgramGraph artifact through direct graph lowering with graph identity, target, `check.lowering: "direct-program-graph"`, target readiness, safety facts, and graph-mapped diagnostics. |
| `zero graph size --json` | Size, helper, runtime, profile, safety, and backend facts for a ProgramGraph artifact lowered through `direct-program-graph`, with graph identity. |
| `zero graph build --json` | Build a ProgramGraph artifact through direct graph lowering, including graph identity, selected `emit` kind, target, artifact path and size, safety facts, compiler cache facts, and graph-aware incremental invalidation. |
| `zero graph patch --json` | Checked graph edits with graph-hash preconditions, per-operation node/field results, the changed graph hash, and the saved source or artifact path. |
| `zero graph roundtrip --json` | Source or ProgramGraph artifact stability through direct graph lowering with `semanticStable`, lowering mode, original and roundtripped graph hashes, raw counts, normalized semantic counts, and optional ProgramGraph output. |
| `zero dev --json` | A watch plan for changed source, manifest, package-lock, and generated-binding inputs. |
| `zero dev --json --trace` | Adds phase timing, cache hit/miss facts, diagnostics passthrough, and `interfaceFingerprints`. |
| `zero time --json` | Compiler phase timing plus `interfaceFingerprints` and incremental invalidation facts. |
| `zero build --json` | Artifact path, size, selected `toolchain`, target triple, linker flavor, sysroot status, `safetyFacts`, and runtime provider facts when a helper such as hosted HTTP is linked. |
| `zero size --json` | `profileSemantics`, `profileCatalog`, `profileBudget`, `safetyFacts`, `sizeBreakdown`, `retentionReasons`, and `optimizationHints`. |
| `zero ship --json` | A release preview with artifact names, hashes, safety facts, a checksum file, debug-symbol metadata, size report, and SBOM placeholder. |
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

## ZDN Output

Refer to the [ZDN Format](/zdn) reference for the complete specification,
syntax grammar, parser example, and output samples for every command.

Use `--zdn` or `--format zdn` when an AI agent or automated tool
will read structured output. ZDN (Zero Data Notation) is an
agent-first format that reuses Zero's row syntax — each
command produces a record with named fields, nested objects, and
arrays.

**Since Zero 0.1.4, `--zdn` output includes all fields previously
available only with `--json`.** Each ZDN record contains the same
compiler metadata — compileTime, metaCache, interfaceFingerprints,
incrementalInvalidation, profileSemantics, toolchain plans, and more —
formatted in ZDN's indentation-based syntax rather than JSON brackets.

`--zdn` is a shorthand for `--format zdn`. Both flags produce
the same output. `--text` (or `--format text`) requests plain
human-readable text explicitly — though text is already the default
when no format flag is given.

| Flag | Output |
| --- | --- |
| _(none)_ | Human-readable text |
| `--text` / `--format text` | Same as default (explicit) |
| `--json` | Structured JSON for tools |
| `--zdn` / `--format zdn` | Structured ZDN for agents |

Supported across all commands that accept `--json`:

ProgramGraph input commands (`validate`, `view`, `check`, `size`, `build`,
`run`, `test`, and `patch`) accept saved `.program-graph` files, canonical graph
`.0` files, or a package directory or `zero.json` when `targets.cli.graph`
points at saved ProgramGraph input. `zero graph roundtrip` uses that
ProgramGraph input for packages that declare it, and otherwise roundtrips source
input.
The direct `check`, `size`, `build`, `run`, `test`, and `ship` commands use
that graph entrypoint for packages that declare it; packages without
`targets.cli.graph` continue to use `targets.cli.main`.
Those direct commands also load a `.0` input as ProgramGraph source storage when
the file starts with the ProgramGraph schema header; ordinary row `.0` files
continue through the row parser.

`.0` files are source text. ProgramGraph commands that write graph artifacts
must use a non-source output path, such as `.zero/out/app.program-graph`.
Agents can inspect source through ProgramGraph commands and can patch canonical
`.0` source through `zero graph patch`. ProgramGraph artifacts remain optional
debug and interchange files.

## ProgramGraph Patches

`zero graph patch` applies checked edits to a graph. When the input is
canonical `.0` source without comments, the command rewrites that source after
lowering, formatting, re-parsing, and semantic graph comparison succeeds. Row
syntax sources such as `examples/hello.0` should be rendered to a canonical view
or patched as ProgramGraph artifacts. For small edits, pass one or more
operations inline:

```sh
zero graph patch \
  .zero/out/hello.view.0 \
  --expect-graph-hash graph:f76987e99677f1b3 \
  --op 'set node="#610c78bf" field="value" expect="hello from zero\n" value="hello patched\n"'
```

For larger edits, patch files are line-oriented text:

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
zero --version [--json] [--zdn]
zero new cli|lib|package <path>
zero doctor [--json] [--zdn]
zero check [--json] [--zdn] [--target <target>] [--emit exe|obj] <input>
zero dev [--json] [--zdn] [--trace] [--target <target>] <input>
zero run [--target <target>] [--profile dev|release] [--out <file>] <input> [-- args...]
zero build [--emit exe|obj] [--target <target>] [--profile dev|release] [--out <file>] <input>
zero ship [--json] [--zdn] [--target <target>] [--profile release-small|tiny|audit] [--out <file>] <input>
zero test [--json] [--zdn] [--filter <name>] [--target <target>] [--cc <path>] [--out <file>] <input>
zero fmt [--check] <input>
zero graph [dump|import|inspect|validate|view|check|size|build|run|test|patch|roundtrip] [--json] [--zdn] [--target <target>] <input> [patch]
zero graph [dump|import|validate|roundtrip] [--json] [--zdn] --out <program-graph-artifact> <input>
zero graph view [--json] [--zdn] [--out <file.0>] <program-graph-or-source>
zero graph size [--json] [--zdn] [--target <target>] --out <artifact> <program-graph-or-package>
zero graph patch [--json] [--zdn] [--out <program-graph-artifact>] <program-graph-or-source> (<patch-file>|--op <operation>)
zero graph build [--json] [--zdn] [--emit exe|obj] [--target <target>] [--profile debug|dev|release-fast|release-small|tiny|audit] [--release <profile>] [--out <file>] <program-graph-or-package>
zero graph run [--zdn] [--target <host-target>] [--profile debug|dev|release-fast|release-small|tiny|audit] [--release <profile>] [--out <file>] <program-graph-or-package> [-- args...]
zero graph test [--json] [--zdn] [--filter <name>] [--target <target>] <program-graph-or-package>
zero doc [--json] [--zdn] [--target <target>] <input>
zero size [--json] [--zdn] [--target <target>] [--out <artifact>] <input>
zero explain [--json] [--zdn] <diagnostic-code>
zero fix --plan [--json] [--zdn] [--target <target>] <input>
zero targets
zero clean [--all]
zero mem [--json] [--zdn] [--target <target>] <input>
zero time --json|--zdn [--target <target>] <input>
zero abi check|dump [--json] [--zdn] [--target <target>] <input>
zero tokens --json|--zdn <input>
zero parse --json|--zdn <input>
```
