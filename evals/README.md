# Zero Evals

TypeScript evals for agent-facing Zero workflows.

Run the checked-in fixture without calling Claude:

```sh
pnpm evals -- --case hello-world --fixture
pnpm evals -- --case rosetta-100-doors --fixture
pnpm evals -- --suite agent-scale --fixture
```

Run live in Vercel Sandbox through Claude Code and Vercel AI Gateway:

```sh
AI_GATEWAY_API_KEY=... pnpm evals -- --case hello-world
```

By default, evals run each selected case against:

- `anthropic/claude-opus-4.7`
- `anthropic/claude-sonnet-4.6`

Override the model set with repeated `--model` flags or a comma-separated
`--models` value:

```sh
pnpm evals -- --case hello-world --model anthropic/claude-sonnet-4.6
pnpm evals -- --case hello-world --models anthropic/claude-opus-4.7,anthropic/claude-sonnet-4.6
```

Live evals create a Vercel Sandbox, upload the current checkout, build the
native compiler, install Claude Code, and run the agent inside the sandbox. Each
model/case run gets a fresh copy of the prepared checkout so mutations from one
run do not affect the next run. The sandbox network policy injects the AI
Gateway bearer credential for `https://ai-gateway.vercel.sh`.

Credential options:

```sh
# Sandbox auth
VERCEL_OIDC_TOKEN=...
# or VERCEL_TOKEN=... VERCEL_TEAM_ID=... VERCEL_PROJECT_ID=...

# AI Gateway auth
AI_GATEWAY_API_KEY=...

# Model selection
ZERO_EVAL_MODELS=anthropic/claude-opus-4.7,anthropic/claude-sonnet-4.6
# or ZERO_EVAL_MODEL=anthropic/claude-sonnet-4.6
```

Each live model run must load Zero's version-matched skill through
`bin/zero skills get zero --full`, then use `bin/zero check` and `bin/zero run`
inside the sandbox to verify its candidate.

The Rosetta cases are deterministic code-challenge evals. Prompts describe the
task behavior and expected output; the evaluator does not compare an exact
projection. It imports the returned source into a graph artifact, checks that
graph, runs it, and compares stdout/stderr plus a small set of source-shape
requirements.

The `agent-scale` suite covers larger agent tasks. It includes multi-command CLI
programs with several runtime checks and graph package fixtures such as a CRM
HTTP request-envelope API. Package cases are validated in place: the evaluator
runs `zero check`, executes each smoke route or command with isolated `--out`
paths, and inspects `zero view` output for required graph/source shape signals.

The eval system prompt intentionally avoids Zero syntax examples. The model is
expected to learn task-relevant syntax from the version-matched skills and
compiler feedback.
