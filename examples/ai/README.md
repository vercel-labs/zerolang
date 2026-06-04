# AI-Native Primitives for Zero — Phase 2 Prototype

This package demonstrates AI-native primitives for Zero, following the design
at https://github.com/vercel-labs/zero/issues/4.

## Design Principles

1. **Capability-gated** — AI functions require an `Ai` capability handle
2. **Effect-visible** — AI calls appear in `graph --json` effects arrays
3. **Budget-aware** — Token/cost limits are declared, not implicit
4. **Human-review boundaries** — Operations can require approval

## Module Structure

- `src/ai.0` — Core AI shapes and methods
- `hello.0` — Example program using AI primitives

## Status

This is a Phase 2 prototype. The code passes `zero check` but cannot yet be
run due to MVP codegen limitations (complex shapes not yet supported by the
direct backend). The design is validated by the compiler's type system and
JSON diagnostics surfaces.

## Next Steps

- Phase 3: Streaming + structured output
- Phase 4: Tool calls + budget enforcement
- Phase 5: Human review boundaries
