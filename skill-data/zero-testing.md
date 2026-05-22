---
name: zero-testing
description: Write and run Zero test blocks with JSON output.
---

# Zero Testing

Use this when adding tests, debugging failing tests, or wiring Zero checks into CI and editor workflows.

## Test Blocks

Zero test blocks live beside source:

```zero
fn add i32 left i32 right i32
  ret + left right

test "addition works"
  expect (== (add 2 3) 5)
```

`expect` requires a `Bool`. A false expectation fails the test.

## Run

```sh
zero test conformance/native/pass/test-blocks.0
zero test --json conformance/native/pass/test-blocks.0
zero test --json --filter addition conformance/native/pass/test-blocks.0
zero test --json conformance/packages/test-app
```

Use `--filter` for a narrow loop. The filter matches test names by substring.

## JSON Fields

Useful `zero test --json` fields:

- `testDiscovery`: how files and tests were found
- `fixtures`: fixture inputs and snapshot metadata
- `snapshotKey`: stable test snapshot contract
- `discoveredTests`, `selectedTests`, `passedTests`, `failedTests`
- `expectedFailures`, `unexpectedPasses`
- `targetFacts`: selected target and capability facts
- `results`: per-test name, status, duration, source location, and failure span
- `stdout`, `stderr`, `durationMs`

Use JSON for agents and CI. Text output is for people.

## Expected Failures

Expected-fail tests use one of these name markers:

- `xfail:`
- `expected fail:`
- `[xfail]`

Example:

```zero
test "xfail: pending parser edge case"
  expect false
```

An expected-fail test passes the command only when it fails as expected. If it starts passing, the command fails with `unexpectedPasses`.

## Agent Workflow

1. Add the smallest test that owns the behavior.
2. Run a filtered test while editing.
3. Run the containing package or fixture before finishing.
4. Do not leave an expected-fail marker on a fixed bug.
5. Use `zero check --json` first when the failure is a compile error.
