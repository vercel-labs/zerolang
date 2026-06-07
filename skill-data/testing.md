---
name: testing
description: Write and run Zero test blocks.
---

# Zero Testing

Use this when adding tests, debugging failing tests, or wiring Zero checks into CI and editor workflows.

## Test Blocks

Zero test blocks live beside source:

```zero
fn add(left: i32, right: i32) -> i32 {
    return left + right
}

test "addition works" {
    expect add(2, 3) == 5
}
```

`expect` requires a `Bool`. A false expectation fails the test.

Use `std.testing` helpers when a predicate should be explicit in the source:

```zero
test "output shape" {
    expect std.testing.equalBytes("zero", "zero")
    expect std.testing.containsBytes("zerolang", "lang")
}
```

`std.testing` does not register tests or print output. It only returns `Bool`
values for ordinary `expect` statements.

## Run

```sh
zero test conformance/native/pass/test-blocks.0
zero test --filter addition conformance/native/pass/test-blocks.0
zero test --target linux-musl-x64 --profile audit conformance/native/pass/test-blocks.0
zero test conformance/packages/test-app
```

Use `--filter` for a narrow loop. The filter matches test names by substring.
When target/profile context matters, pass `--target` and `--profile`; JSON
`agentCommand.command.argv` and `verificationCommands` preserve that context.
Verification commands include `purpose`, `required`, and `argv`; run the
required `source-check` proof before the required `test-run` proof.

For normal agent edits, patch the backing `.0` source and run `zero test`. When an agent is explicitly validating a derived ProgramGraph artifact, use the graph test surface:

```sh
zero graph test .zero/agent/app.program-graph
zero graph test --filter addition .zero/agent/app.program-graph
zero graph test --target linux-musl-x64 --profile audit .zero/agent/app.program-graph
```

Run normal `zero test` after persisting the accepted change to source text.

## JSON Fields

Use `zero test --json` when a tool or CI job needs exact fields. Useful fields:

- `agentCommand`: canonical test argv, stable audit fields, failure fields, and
  verification commands for Agent repair loops
- `testDiscovery`: how files and tests were found
- `fixtures`: fixture inputs and snapshot metadata
- `snapshotKey`: stable test snapshot contract
- `discoveredTests`, `selectedTests`, `passedTests`, `failedTests`
- `expectedFailures`, `unexpectedPasses`
- `targetFacts`: selected target and capability facts
- `profile`: selected profile for replay and verification
- `results`: per-test name, status, duration, source location, and failure span
- `stdout`, `stderr`, `durationMs`

Use JSON for machines and CI contracts. Normal test output is the default agent loop.

For ProgramGraph artifacts, use `zero graph test --json <artifact-or-package>`.
Its `agentCommand` is graph-specific: `command.argv` replays `zero graph test`,
`stateFields` carries `graph.graphHash`, lowering, target, profile, and filter state,
`failureFields` points to per-test failure and location spans, and
`verificationCommands` rerun `zero graph check --json` plus the same graph test.
Replay those verification commands directly; they preserve target/profile and
filter context. Use `purpose: "graph-check"` for semantic validity and
`purpose: "graph-test"` for the filtered test proof.
When `failedTests > 0` or `unexpectedPasses > 0`, prefer
`agentCommand.recommendedNextCommands[]` for the optional `graph-inspect`
follow-up. It preserves the same target/profile context and returns graph hashes
plus semantic node ids before planning a repair.
When `selectedTests == 0`, prefer the optional `doc-audit` follow-up from
`agentCommand.recommendedNextCommands[]` to inspect public symbols and missing
example/test coverage before claiming behavioral confidence.

## Expected Failures

Expected-fail tests use one of these name markers:

- `xfail:`
- `expected fail:`
- `[xfail]`

Example:

```zero
test "xfail: pending parser edge case" {
    expect false
}
```

An expected-fail test passes the command only when it fails as expected. If it starts passing, the command fails with `unexpectedPasses`.

## Agent Workflow

1. Add the smallest test that owns the behavior.
2. Run a filtered test while editing.
3. Run the containing package or fixture before finishing.
4. Do not leave an expected-fail marker on a fixed bug.
5. Use `zero check` first when the failure is a compile error; rerun with `--json` only if you need exact diagnostic fields.
