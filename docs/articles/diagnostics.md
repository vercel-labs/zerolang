## Reading An Error

A typical error looks like this:

```text
error[NAM003]: Unknown identifier
  unknown identifier 'message'
  examples/hello.0:2:27

  2 |     check world.out.write(message)
    |                           ^^^^^^^
  rule: Names must be declared before use in the current lexical scope.
  expected: local binding, parameter, function, builtin value
  actual: no visible symbol named 'message'
  fix: Introduce a local binding before this use (local-edit)
  explain: zero explain NAM003
```

Read it from top to bottom:

- The first line gives the stable code and short title.
- The message says what went wrong.
- The span points to the file, line, and column.
- The source excerpt marks the exact token.
- `rule`, `expected`, and `actual` explain the mismatch.
- `fix` gives the safest repair shape.
- `explain` points to deeper help.

In this case, the program tried to write `message` without declaring it. A local binding fixes it:

```zero
pub fn main(world: World) -> Void raises {
    let message: String = "hello from zero\n"
    check world.out.write(message)
}
```

## Plain Text By Default

Default diagnostics should be short and useful in terminal logs.

They should not include ANSI colors, bold styling, hyperlinks, OSC escapes, or
terminal control sequences by default. Those bytes bloat agent context and make
logs harder to compare.

Use:

```sh
zero check examples/hello.0
```

## JSON For Tools

JSON is explicit. Use `--json` for agents, CI, editors, deep dives, and tools that need stable structured data.

```sh
zero check --json examples/hello.0
```

The native JSON shape is versioned:

```json
{
  "schemaVersion": 1,
  "ok": false,
  "diagnostics": [
    {
      "severity": "error",
      "code": "NAM003",
      "message": "unknown identifier 'message'",
      "path": "examples/hello.0",
      "line": 2,
      "column": 27,
      "length": 7,
      "expected": "visible local, parameter, function, or builtin",
      "actual": "no visible symbol named 'message'",
      "help": "declare the name before using it",
      "fixSafety": "behavior-preserving",
      "repair": {
        "id": "manual-review",
        "summary": "Inspect the diagnostic fields and choose a repair manually."
      },
      "related": []
    }
  ]
}
```

## Fix Safety

Fixes carry safety labels so agents know when they can act:

- `format-only`: changes only formatting
- `behavior-preserving`: preserves program behavior
- `local-edit`: confined to the current local scope or file
- `api-changing`: changes function signatures, exported names, package APIs, or call sites
- `requires-human-review`: risky or ambiguous; show the plan but do not apply automatically

## Current Native Diagnostics

The native compiler keeps stable codes for implemented control-flow and type rules:

- `PAR100`: parser syntax failures such as missing braces, commas, or malformed type argument lists
- `NAM003`: unknown identifiers
- `NAM004`: duplicate names, wrong call arity, or generic type-name shadowing
- `IMP001`: unknown package-local imports, with repair id `fix-import-path`
- `IMP002`: package-local import cycles
- `IMP003`: duplicate public exports across imported modules
- `PKG001`: a local package dependency path does not contain `zero.json`
- `PKG002`: package dependencies form a cycle
- `PKG003`: one package name resolves to conflicting versions
- `PKG004`: a package dependency does not support the selected target
- `BLD002`: bad project manifest or unsupported manifest target shape
- `ERR002`: a caller's explicit error set is missing an error raised by a callee
- `ERR003`: a fallible call was used without `check` or `rescue`
- `ABI001`: unsupported C ABI export or extern layout surface
- `CIMP003`: a foreign-target C dependency would use host include paths, host library paths, or implicit host `pkg-config` discovery
- `CIMP004`: an extern C call names a function that is missing from the imported header or uses an unsupported C ABI type
- `CIMP005`: an extern C call is missing matching package C link metadata or uses an unsafe system library name
- `BOR001`: lexical borrow conflicts, with JSON `borrowTrace.activeBorrows`
  entries naming each reported borrowed root, path, kind, live binding,
  declaration range when known, and repair shape. `borrowTrace.truncated` is
  true if the report hit the cap.
- `BOR002`: reference-origin escapes, including references returned from calls or stored through mutable parameter storage
- `OWN001`: owned value use after move, or generic containers that would own unconstrained generic payloads
- `TYP010`: conditions must be `Bool`
- `TYP001`: call argument type does not match the callee parameter annotation
- `TYP002`: type mismatch in assignments, literals, returns, or type defaults
- `TYP003`: return expression type does not match the function return annotation
- `TYP011`: `null` requires a `Maybe<T>` context
- `TYP012`: `break` requires an enclosing loop
- `TYP013`: `continue` requires an enclosing loop
- `TYP014`: range loop bounds must be integer-compatible
- `TYP015`: an integer literal must use valid digits, separators, radix prefixes, and integer suffixes
- `TYP016`: an integer literal must fit the expected primitive integer width
- `TYP017`: `as` casts are limited to primitive numeric and byte `char` source and target types
- `TYP018`: a character literal must contain exactly one byte or a supported byte escape
- `TYP019`: a float literal must use `digits "." digits` with an optional exponent
- `TYP020`: a float literal must fit the expected primitive float width
- `TYP021`: indexing, slicing, and indexed assignment require a supported target for that operation
- `TYP022`: index expressions and present slice bounds must be integers
- `TYP023`: generic call type argument count mismatch, or type arguments on a non-generic function
- `TYP024`: generic inference found conflicting concrete types for one type parameter
- `TYP025`: generic type arguments could not be inferred from local call arguments
- `TYP026`: a type alias is duplicated, malformed, or cyclic
- `TYP027`: recursive generic call changes type arguments
- `PUB001`: a public declaration omitted required explicit API type metadata
- `MET001`: a parsed `meta` expression requested compile-time behavior this compiler slice cannot evaluate yet
- `IFC001`: an interface constraint is unknown or a concrete type argument has no static type body
- `IFC002`: a constrained concrete type is missing a required static interface method
- `IFC003`: a concrete static method has the wrong parameter count for an interface
- `IFC004`: a concrete static method has the wrong return type for an interface
- `IFC005`: a concrete static method has the wrong parameter type for an interface
- `STC001`: a static value parameter uses an unsupported non-integer type
- `STC002`: a static value argument is not an integer literal or deterministic top-level const
- `STC003`: an explicit static value argument conflicts with the value carried by an annotated type
- `SHM001`: a generic type method call cannot infer inherited type/static parameters
- `SHM002`: arguments to a generic type method imply conflicting `Self` instantiations
- `RCV001`: a receiver-style call names an unknown method or a static method without `self`
- `RCV002`: a receiver-style call needs an addressable receiver, or a mutable receiver for `mutref<Self>`
- `FLD001`: a type literal includes an unknown field
- `FLD002`: a type literal omitted a required field that has no default
- `MEM002`: a `Maybe<T>.value` payload read requires a visible `.has` guard, `check`, or `rescue`
- `TAR001`: the requested target name is not in `zero targets`; JSON command
  failures require `zero targets --json` before retrying with a
  `<supported-target>` value, preserving repair modes and graph patch inputs
- `TAR002`: the selected target does not provide a capability required by the program
- Bounds check failures: native executables print `zero bounds check failed` and
  abort when an index, indexed assignment, or slice range is outside the base
  length.
- `MAT004`: a match arm can bind a payload only for a choice case that carries one

## Standard Library Diagnostics

Standard library modules use the same structured diagnostic contract as compiler diagnostics.

- `MEM001` reports malformed memory type forms such as `Maybe` without its required type argument.
- `MEM002` reports a `Maybe<T>.value` read that has not been proven present by a visible `.has` guard.
- `std.parse`, `std.json`, and `std.env` diagnostics carry source spans where
  applicable.
- `std.time` diagnostics can use offset-only spans for single-token inputs.
- Standard library codes stay stable and package-local, while `zero explain <code>` provides human guidance and `--json` exposes structured fix metadata.

## Common Fix Plans

Hosted filesystem helpers are host-only in the current compiler. This fails clearly on non-host targets:

```sh
bin/zero check --json --target linux-musl-x64 conformance/native/fail/std-fs-target-unsupported.0
```

The diagnostic uses `TAR002`, `fixSafety: "requires-human-review"`, and repair
id `choose-target-with-required-capability`.

The canonical repair is to build for a target with the required capability or
move that code behind a target-specific entry point.

Writable byte helpers require mutable storage:

```zero
let dst: [4]u8 = [0, 0, 0, 0]
let src: [4]u8 = [122, 101, 114, 111]
let _copied: usize = std.mem.copy(dst, src)
```

This reports `TYP009` with repair id `make-binding-mutable`. The canonical repair is:

```zero
var dst: [4]u8 = [0, 0, 0, 0]
let src: [4]u8 = [122, 101, 114, 111]
let _copied: usize = std.mem.copy(dst, src)
```

If the destination is repaired but the receiving binding still uses the wrong
annotation, `zero check --json` reports `TYP002` with repair id
`match-binding-annotation`. For simple binding annotation mismatches,
`zero fix --apply --json` can change the annotation to the initializer's
reported `actual` type, after which the agent should rerun check before graph
edits.

Named-error `std.fs` calls require explicit error flow:

```zero
let file: owned<File> = std.fs.createOrRaise(fs, ".zero/out.txt")
```

This reports `ERR003` with repair id `check-or-rescue-fallible-call`.

Use `check` and include `NotFound`, `TooLarge`, and `Io` in the caller's
`raises [NotFound, TooLarge, Io]` set. Use `rescue` locally when the call should recover in
place.

Generic calls use local inference only. This fails because `T` would need to be both `i32` and `u8`:

```zero
fn first<T: Type>(left: T, right: T) -> T {
    return left
}

let value: i32 = first(1, 2_u8)
```

The repair is to make the arguments agree or pass explicit type arguments with compatible values:

```zero
let value: i32 = first<i32>(1, 2)
```

Public constants need explicit API shape:

```zero
pub const answer = 42
```

This reports `PUB001`. The behavior-preserving repair is:

```zero
pub const answer: i32 = 42
```

C interop keeps host and target discovery separate. This fails for a foreign target because the manifest asks for host include/library discovery:

```sh
bin/zero build --json --target linux-musl-x64 conformance/c/host-leak-package --out .zero/out/host-leak-package
```

This reports `CIMP003` with repair id `configure-target-c-dependency`.

The canonical repair is to use package-relative vendored headers/libraries or
configure the target sysroot. Do not rely on host include paths, host library
paths, or host `pkg-config` discovery for cross-target builds.

Extern C calls also require a matching link plan in `zero.json`. The imported
header must appear in `c.libs.*.headers`, and that library must provide `lib` or
`link` inputs. Missing matching inputs and unsafe `link` names report `CIMP005`
with repair id `configure-c-link-plan`.

Package dependency diagnostics are graph-level repairs:

| Code | Meaning |
| --- | --- |
| `PKG001` | A local dependency path is wrong or missing. |
| `PKG002` | Two package manifests depend on each other cyclically. |
| `PKG003` | The graph resolved one package name to multiple versions. |
| `PKG004` | The selected target is outside the dependency's target list. |

These all use `requires-human-review` because the correct repair may change
package topology or target support.

Type aliases are compile-time spellings and cannot cycle:

```zero
alias A = B

alias B = A
```

This reports `TYP026`. Point the alias at a concrete type such as `Span<u8>` or
remove the cycle.

Unsupported compile-time execution reports `MET001`, for example
`const os String meta target.os`, until target facts are implemented in the
native compiler.

Generic type methods must specialize from a concrete `Self` value or explicit type arguments:

```zero
type FixedVec<T: Type, static N: usize> {
    fn cap() -> usize {
        return N
    }
}

let cap: usize = FixedVec.cap()
```

This reports `SHM001`.

Repair it in one of two ways:

- pass explicit type arguments, such as `FixedVec.cap<u8, 4>()`
- call a method that receives a concrete `Self` value, such as
  `FixedVec.push(&mut vec, value)` or `vec.push(value)`

`SHM002` means the explicit method arguments and the receiver's annotated type
disagree.

Receiver calls require a declared method whose first parameter is `self: ref<Self>` or `self: mutref<Self>`:

```zero
let vec: FixedVec<u8, 4> = FixedVec { len: 0, items: [0, 0, 0, 0] }
check vec.push(1)
```

This reports `RCV002` because `push` needs `mutref<Self>` and `vec` is
immutable.

Unknown receiver methods report `RCV001`. Static methods without `self` should
be called through the type namespace.

Type field defaults allow omitted fields, but only when the declaration provides a compatible default:

```zero
type NeedsItem {
    count: usize = 0,
    item: u8,
}

let value: NeedsItem = NeedsItem {}
```

This reports `FLD002` with repair id `initialize-missing-field` because `item`
has no default. A default with the wrong type reports `TYP002` at the default
expression.

Static interfaces are checked at generic specialization time. This fails because `Counter` does not provide the required static method:

```zero
type Counter {
    value: i32,
}

interface Readable<T: Type> {
    fn read(self: ref<T>) -> i32
}

fn readValue<T: Readable<T>>(value: ref<T>) -> i32 {
    return T.read(value)
}
```

This reports `IFC002`. Add a concrete static method with the matching signature:

```zero
fn read(self: ref<Self>) -> i32 {
    return self.value
}
```

Static value parameters are checked before emission so fixed-size layouts stay concrete:

```zero
type FixedVec<T: Type, static N: usize> {
    len: usize,
    items: [N]T,
}

fn first<T: Type, static N: usize>(vec: ref<FixedVec<T, N>>) -> T {
    return vec.items[0]
}

let vec: FixedVec<u8, 4> = FixedVec { len: 4, items: [1, 2, 3, 4] }
let bad: u8 = first<u8, 8>(&vec)
```

This reports `STC003` because the explicit `8` conflicts with the annotated
`FixedVec<u8,4>`.

Related static-value diagnostics:

- `STC001`: unsupported static parameter type
- `STC002`: runtime value used where a compile-time integer is required

## Commands

- `zero check <input>`: human-first plain text by default
- `zero check --json <input>`: full diagnostic JSON
- `zero explain <code>`: human explanation for a diagnostic code
- `zero explain <code> --json`: machine-readable explanation
- `zero fix --plan --json <input>`: proposed typed fixes without editing files
- `zero fix --patch --json <input>`: compiler-generated patch lines for supported repairs
- `zero fix --apply --json <input>`: apply supported behavior-preserving repairs

Successful `zero check --json` reports include `agentCommand`, a replayable
command contract with canonical `command.argv`, committed `auditFields`,
`diagnosticFields`, and verification commands. Agents should use that contract
when replaying checks instead of reconstructing omitted defaults such as target
or profile flags. Its verification command includes `purpose: "source-check"`
and `required: true`.
Failing `zero check --json` diagnostics include `repair.agentRepair`, a
structured entrypoint into the repair transaction surface. Its required
`repair-plan` command is the safe default, and optional `repair-patch` /
`repair-apply` commands are explicit review and write paths into the same
`agentTransaction` contract. Each command carries `purpose`, `required`, and
`argv`, preserving target/profile flags so agents do not have to reconstruct
`zero fix` invocations from prose.

`zero fix --plan --json`, `zero fix --patch --json`, and `zero fix --apply --json`
include `agentTransaction`, a stable transaction outline for agents. It records
the repair id, diagnostic code, transaction phases, graph-patch preconditions,
checked edit surface, failure class, rollback metadata, and verification
commands. Treat this object as the compiler-owned repair contract: inspect the
diagnostic, plan the edit, apply a checked source or graph patch, then rerun the
listed verification commands. `resultFields` and `failureClasses` describe the
stable machine fields and rejected-transaction classes an agent should depend on.
For source repairs, `resultFields` includes the input path, executed
`command.argv`, diagnostic code, repair id, rollback fields, verification
commands, `verificationCommands[].purpose`,
`verificationCommands[].required`, and failure class, phase, retryable state,
retry commands, `failure.retryCommands[].purpose`,
`failure.retryCommands[].required`, and applied state.
`writePolicy` is `preview-only` for `--plan` and `--patch`, and `writes-source`
for `--apply`.
Source repair transactions list both
`zero check --json` and `zero graph check --json` so agents can verify type
correctness and ProgramGraph consistency before continuing. Each verification
command includes `purpose`, `required`, and `argv`; execute every required
command and record whether the proof is a source check or graph check. If
`agentTransaction.ok` is false, use the
structured `failure.retryCommands` and `failure.retryStrategy` instead of
guessing from terminal text. Check `failure.retryable` and
`failureClasses[].retryable` before retrying automatically. `failure.phase`
identifies whether the rejected
source repair failed during inspect, patch, or rewrite. A `source-unavailable`
transaction reports `zero check --json <input>` as its inspect retry. A `write-failed`
transaction reports `zero fix --apply --json <input>` as its retry command after
the agent checks file permissions or the output path.
Retry command objects include `purpose`, `required`, and `argv`; use
`source-check`, `repair-plan`, `graph-inspect`, or `repair-apply` to route the
next step without parsing the command string.
If a compiler source repair would be a no-op or the diagnostic span no longer
matches a supported single-line replacement, the transaction is rejected as
`edit-not-found` and reports `zero check --json <input>` for replanning.
`agentTransaction.phaseAudit` records whether inspect and plan completed, whether
a source patch is ready, whether source patch preconditions validated, whether
rewrite was applied, and whether verification commands were scheduled.
Every `zero check --json` and `zero fix --plan/--patch/--apply --json`
diagnostic includes `graphLookup`. That object gives graph inspection commands,
the diagnostic span, and the ProgramGraph node fields (`path`, `line`, `column`)
an agent should match before reading broader source. Use it to map
diagnostic-driven work back to a semantic node neighborhood. Lookup commands
include `purpose`, `required`, and `argv`; use `graph-inspect` for semantic
facts and `graph-check` for verification without parsing command strings.
If a graph inspection command still fails because the program has a type error,
its JSON response can include `agentRecovery` and `recoverableProgramGraph`.
Treat that graph as parse-level context for lookup and repair planning only, then
rerun the listed verification commands after applying a checked edit. Recovery
verification commands carry `purpose`, `required`, and `argv` fields.
For source repairs, `agentTransaction.patchContract` describes the `patches`
array as single-line replacements with required `path`, `line`, `old`, and `new`
fields. Agents should treat `old` as a precondition and refuse to apply a patch
when the current line or diagnostic span no longer matches. `rollback.restoreFields`
points at the `patches[].path`, `patches[].line`, and `patches[].old` fields an
agent can use for a reviewed source rollback.
Each item in `fixes[]` also includes `repairContract`, which records the
required diagnostic inputs, whether the compiler can generate an automatic
source patch for that repair id, the patch source, preconditions, and the
verification command shape. This lets an agent decide whether to request
`--patch`, fall back to `zero graph patch`, or require human review without
parsing prose. `repairContract.verification[]` uses the same `purpose`,
`required`, and `argv` shape as transaction verification commands.
Supported repairs can also include `fixes[].graphPatchCandidates[]`. These are
compiler-generated checked edit candidates with a graph hash, target node,
expected value, patch text, preconditions, evidence, and
`zero graph patch --json` command shape. `evidence` records the diagnostic span,
semantic target node, and typed values used to prove the candidate. Agents
should inspect this object before applying the patch, then use the operation
result fields and verification commands for the audit trail. Candidate
verification commands also include `purpose`, `required`, and `argv`. This lets an agent
move from a diagnostic directly into a checked graph transaction without
inventing a line patch.
Each candidate includes `candidateContract`, which declares the patch kind, the
structured command field, the `patch.text` field that must be written to a patch
file, state fields such as `patch.graphHash`, audit fields, and the graph patch
transaction result contract to expect after submission.
If the source or graph changes after planning, submit the candidate anyway only
through `zero graph patch --json`; stale graph hashes are rejected as structured
transactions with `agentTransaction.failure.retryCommands` for reinspection.
The repair planner emits these candidates for supported mutability, parameter
annotation, binding annotation, field default annotation, return annotation, and
narrowly proven missing-symbol repairs. For `TYP001`, a direct call argument
mismatch may produce `changeParamType` when the call resolves to one function,
the argument node type matches the diagnostic `actual`, and the corresponding
parameter type matches `expected`. The argument type may come from a typed
literal, same-function parameter, or prior same-function `let` binding; treat it
as human-reviewed because it changes the function signature. For `TYP002`, a
binding mismatch may produce `changeLocalType` when the initializer expression
type matches the diagnostic `actual` field. The initializer proof may come from
a typed literal, same-function parameter, prior same-function `let` binding, or
known stdlib helper return signature. A shape field default mismatch whose
default expression type matches the diagnostic `actual` field may produce
`changeFieldType`.
For `TYP003`, a return mismatch whose return expression type matches the
diagnostic `actual` field may produce a `changeReturnType` candidate. The return
expression type may come from a typed literal, same-function parameter, or prior
same-function `let` binding, or known stdlib helper return signature; treat it
as human-reviewed because it changes the function signature. For `NAM003`, an
unresolved typed call may produce `replaceCallee` when exactly one existing
function has the required return type and parameter types, or `addFunction` when
no replacement is proven and a typed result context such as
`let value: i32 = missing()` or `return missing(input)` inside
`fn run(input: i32) -> i32` can scaffold `fn missing(...) -> i32 { return 0 }`.
When call argument types are proven from literals, same-function parameters, or
prior same-function `let` bindings, the candidate includes a checked `params`
signature, for example `missing(41)` can scaffold
`fn missing(arg0: i32) -> i32 { return 0 }`. Calls with non-inferable arguments
are deliberately left without a candidate until the planner can validate a full
signature.

Useful examples:

```sh
zero explain TAR002
zero explain --json TYP009
zero fix --plan --json conformance/native/fail/mem-copy-immutable-dst.0
zero fix --patch --json examples/agent-repair-demo/broken.0
zero fix --apply --json examples/agent-repair-demo/broken.0
zero fix --plan --json --target linux-musl-x64 conformance/native/fail/std-fs-target-unsupported.0
```
