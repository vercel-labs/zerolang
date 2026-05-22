# Agent Repair Demo

This demo shows the intended agent loop on a real diagnostic.

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

Apply the suggested edit:

```diff
-  let dst [4]u8 [0, 0, 0, 0]
+  mut dst [4]u8 [0, 0, 0, 0]
```

Re-run check:

```sh
bin/zero check examples/agent-repair-demo/fixed.0
```

Run the scripted demo:

```sh
pnpm run agent:demo
```
