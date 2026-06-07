# Agent Repair Demo

This demo shows the intended agent loop on real compiler-mediated
transactions.

Broken fixture:

```sh
bin/zero check --json examples/agent-repair-demo/broken.0
```

Explain the diagnostic:

```sh
bin/zero explain --json TYP009
```

Inspect the repair plan:

```sh
bin/zero fix --plan --json examples/agent-repair-demo/broken.0
```

Apply compiler-owned repair transactions until the file checks:

```sh
bin/zero fix --apply --json .zero/agent-repair-demo/main.0
bin/zero check --json .zero/agent-repair-demo/main.0
bin/zero fix --apply --json .zero/agent-repair-demo/main.0
```

The first transaction repairs `TYP009` by changing immutable storage to `var`.
The second repairs `TYP002` by changing the `_copied` binding annotation from
`i32` to the initializer's actual `usize` type.

Agents can run the same repair sequence without source-line patches by using
`zero fix --plan --json` and submitting each
`fixes[].graphPatchCandidates[].patch.text` through `zero graph patch --json`.
Read `fixes[].graphPatchCandidates[].candidateContract` first: it names the
structured command field, the patch text field to write into a temporary patch
file, the state fields to carry, and the graph patch transaction result contract
to verify after submission.
The graph-only smoke records the checked graph transaction audit for each step:

```sh
pnpm run agent-repair:graph-loop
```

Once the file checks, the scripted demo switches to ProgramGraph:

```sh
bin/zero graph dump --json --out .zero/agent-repair-demo/main.program-graph .zero/agent-repair-demo/main.0
bin/zero graph patch --json --out .zero/agent-repair-demo/main.patched.program-graph .zero/agent-repair-demo/main.program-graph --patch-text '<checked patch>'
bin/zero graph validate .zero/agent-repair-demo/main.patched.program-graph
bin/zero graph check .zero/agent-repair-demo/main.patched.program-graph
```

Run the scripted demo:

```sh
pnpm run agent:demo
```
