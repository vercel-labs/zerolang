# AI-Native Primitives for Zero — Design Plan

## Vision

Zero is "the programming language for agents." Today it has agent-friendly *compiler* surfaces (JSON diagnostics, repair plans, target facts). The next step is agent-friendly *language* surfaces — making AI actions first-class citizens of the type and effect system, not hidden behind opaque HTTP calls.

## Core Principle

**AI actions should be explicit effects, not ambient network calls.**

Just like `std.fs` requires the `Fs` capability and `std.net` requires the `Net` capability, AI operations require explicit capability handles. This means:
- `graph --json` reports AI effects alongside fs/net effects
- `check --json` rejects missing provider/budget declarations
- `doctor --json` reports provider/key/runtime readiness
- `fix --plan --json` preserves human-review boundaries for AI-generated edits

## Design Constraints

1. **Host-only** — AI calls need a runtime. Start as `host` target only, like `std.fs`.
2. **Capability-gated** — AI functions require capability handles, not ambient globals.
3. **Effect-visible** — AI calls show up in `graph --json` effects arrays.
4. **Budget-aware** — Token/cost/latency limits are declared, not implicit.
5. **Human-review boundaries** — Certain AI operations require approval before execution.
6. **Subagent-testable** — AI primitives are tested by spawning subagents, not by mocking HTTP calls.

## Modular Design Decisions

### Q1: Provider Abstraction

**Per-provider modules under a shared `std.ai` trait interface.**

```
std.ai              — Core types, traits, capability shapes
std.ai.openai       — OpenAI provider
std.ai.anthropic    — Anthropic provider
std.ai.google       — Google provider
```

Each provider implements the `AiProvider` trait. Programs can be provider-agnostic or provider-specific. This mirrors how `std.fs` is one module but could have `std.fs.s3` later.

### Q2: Streaming

**Separate function with a shared request type.**

- `generateText()` — simple completions
- `generateTextStream()` — returns `AiStream` handle
- Both use the same `GenerateTextRequest`

### Q3: Tool Schemas

**Zero shapes first, JSON schema escape hatch.**

- `callTool<TInput, TOutput>()` — type-safe, appears in `graph --json`
- `callToolRaw()` — dynamic tool discovery

### Q4: Budget Granularity

**All three, layered:**

- Per-call: `maxTokensPerCall`
- Per-session: `maxTokensPerSession`, `maxCostPerSession` (tracked by provider handle)
- Per-call override: `GenerateTextRequest.maxTokens`

### Q5: Approval Mechanism

**Both compile-time effect annotation + runtime check.**

- Compile-time: `effects { AiGenerateCode }` tells the type system this needs approval
- Runtime: `requireApproval` flag enforces the actual approval flow
- Shows in `graph --json` as `approvalBoundary: true`

### Q6: Testing Strategy

**Subagent-based testing, not HTTP mocking.**

An agent tests AI primitives by:
1. Spawning a subagent with a specific AI capability
2. Running the Zero program under test
3. Verifying the output via `check --json` / `graph --json` surfaces
4. The subagent's own reasoning validates the AI-generated output

This is the meta-loop: Zero code calling AI, tested by an AI agent, verified by the compiler's structured JSON diagnostics. No mock servers, no fixture files — the agent *is* the test harness.

## Module Structure

```
std.ai/
  ai.0           — Core types, traits, shapes
  stream.0       — Streaming support
  tools.0        — Tool call abstractions
  budget.0       — Budget tracking and enforcement
  approval.0     — Human review boundary types
  subagent.0     — Subagent spawning and coordination

std.ai.openai/
  provider.0     — OpenAiProvider shape and impl

std.ai.anthropic/
  provider.0     — AnthropicProvider shape and impl
```

## Core Types

```zero
// === std.ai.ai ===

trait AiProvider {
    fun generateText(self: ref<Self>, request: GenerateTextRequest) -> AiResult raises { AiError }
    fun generateTextStream(self: ref<Self>, request: GenerateTextRequest) -> AiStream raises { AiError }
}

shape GenerateTextRequest {
    prompt: String,
    maxTokens: Maybe<i32>,
    temperature: Maybe<f64>,
    systemPrompt: Maybe<String>,
}

shape AiResult {
    text: String,
    tokensUsed: i32,
    cost: f64,
    latencyMs: i32,
    finishReason: String,      // "stop", "length", "budget_exceeded"
}

shape AiError {
    code: String,              // "budget_exceeded", "provider_error", "approval_required", "timeout"
    message: String,
    retryable: Bool,
    providerCode: Maybe<String>,
}

shape AiBudget {
    maxTokensPerCall: i32,
    maxTokensPerSession: i32,
    maxCostPerSession: f64,
    maxLatencyMs: i32,
    requireApproval: Bool,
}

// === std.ai.subagent ===

shape Subagent {
    provider: ref<AiProvider>,
    task: String,
    budget: AiBudget,
}

trait SubagentRunner {
    fun spawn(self: ref<Self>, subagent: Subagent) -> SubagentHandle raises { AiError }
    fun collect(self: ref<Self>, handle: SubagentHandle) -> AiResult raises { AiError }
}

shape SubagentHandle {
    id: String,
    status: String,            // "running", "completed", "failed", "awaiting_approval"
}
```

## Effect System Integration

```zero
// A function using AI declares it in effects:
pub fun main(world: World, ai: OpenAiProvider) -> Void raises {
    let result = check ai.generateText(GenerateTextRequest {
        prompt: "Explain Zero effects",
        maxTokens: 100,
    })
    check world.out.write(result.text)
}
// effects: ["world", "ai.generateText"]
// requiresCapabilities: ["world", "ai"]
```

In `graph --json`:
```json
{
  "name": "main",
  "effects": ["world", "ai.generateText"],
  "requiresCapabilities": ["world", "ai"],
  "approvalBoundary": false
}
```

In `doctor --json`:
```json
{
  "checks": [
    {"name": "ai-provider", "status": "ok", "message": "OpenAI API key found"},
    {"name": "ai-budget", "status": "ok", "message": "Budget: 4096 tokens/call, $0.05/session"}
  ]
}
```

## Implementation Path

### Phase 1: Design Note
- Post as comment on issue #4
- Get feedback from Zero team

### Phase 2: Minimal Prototype
- Add `std.ai.ai` with core types
- Add `std.ai.subagent` with subagent spawning
- Add one provider: `std.ai.anthropic` (or whichever is available)
- Implement `generateText` only
- Surface `ai.generateText` in `graph --json` effects
- Add `examples/ai/hello.0`

### Phase 3: Streaming + Structured Output
- Add `std.ai.stream`
- Add `generateTextStream`
- Add `generateStructured<T>` with Zero shape schemas

### Phase 4: Tool Calls + Budgets
- Add `std.ai.tools`
- Add `callTool<TInput, TOutput>`
- Add budget enforcement to provider handles

### Phase 5: Approval Boundaries
- Add `std.ai.approval`
- Integrate with `fix --plan --json` for AI-generated code changes
- Add `doctor --json` readiness checks

## Example: Subagent-Based Testing

This is the key differentiator. Instead of mocking HTTP calls, agents test AI primitives by spawning subagents:

```zero
// examples/ai/test-hello.0
// This test spawns a subagent that runs the AI program and validates output

use std.ai
use std.ai.subagent

pub fun main(world: World, ai: AiProvider) -> Void raises {
    // Spawn a subagent to test the AI primitive
    let handle = check ai.spawn(Subagent {
        provider: ai,
        task: "Generate a one-sentence explanation of Zero effects",
        budget: AiBudget {
            maxTokensPerCall: 100,
            maxTokensPerSession: 100,
            maxCostPerSession: 0.01,
            maxLatencyMs: 10000,
            requireApproval: false,
        },
    })

    // Collect the result
    let result = check ai.collect(handle)

    // Validate via structured diagnostics, not string matching
    if result.tokensUsed > 0 && result.cost > 0.0 {
        check world.out.write("ai primitive test passed\n")
    } else {
        check world.out.write("ai primitive test failed\n")
    }
}
```

The compiler's `check --json` surface gives the agent structured feedback about the AI effects, budgets, and capabilities — no prose parsing needed.

## Relationship to Existing Work

- Issue #4: This design doc
- Issue #5: `generateText` experiment (subsumed)
- PR #45 (fix-plan edits): Approval boundaries for AI-generated code
- PR #49 (effects summary): AI effects in effectsSummary
- PR #50 (explain catalog): AI diagnostic codes in the catalog

## Next Steps

1. Post this design as a comment on issue #4
2. Build Phase 2 prototype
3. Iterate based on maintainer feedback
