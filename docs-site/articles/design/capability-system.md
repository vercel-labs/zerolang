# Capability System Design for Zero

> Design proposal for making Zero's capability model first-class, addressing the critique in issue #72 while preserving Zero's agent-first design goals.

## Problem Statement

Zero's current capability system has a documentation-implementation gap. The docs say "capability-based effects" but the implementation is closer to dependency injection. This matters more for Zero than for most languages because **agents need static, machine-readable effect information to reason about code safety and composability**.

### Current State (v0.1.2)

1. `World` is a parameter, not a capability token. It's a plain record — storable, duplicable, aliasable.
2. `raises` is a boolean with no effect information. `fun foo() -> Void raises` could do anything.
3. Capabilities like `Fs`, `Net`, `Proc` are mintable from nothing (`std.fs.host()`), making them unforgeable in documentation only.
4. Dual API shapes coexist: `std.fs.read(path, buf)` (ambient) and `std.fs.open(fs, path)` (capability-gated). The ambient form always wins.
5. Effect tracking is done by scanning stdlib call names, not by type-level effect annotations.
6. `graph --json` reports capabilities as a flat boolean struct, not as a structured effect system.

### What Issue #72 Gets Right

The critique is technically accurate on all five structural points:
- Value-level, not type-level, effect tracking
- Coarse-grained god capability (World bundles everything)
- No substructural discipline (World is copyable/aliasable)
- No sequencing or handler structure
- Three orthogonal mechanisms (World parameter, raises, std target-gating) instead of a unified model

### What Issue #72 Misses

The critique evaluates Zero against mature PL research languages (Koka, Pony, E). But Zero has a different target: **agentic programming**. The capability system must serve two masters:
1. **Human programmers** who want readable, writable code
2. **AI agents** who need static, structured, machine-readable effect information

These goals are not in tension. Good effect systems help both. But the design must be incremental — Zero is v0.1.2, not a research language with 10 years of backing.

## Design Principles

### P1: Agent-First Static Information

Every effect a function can perform must be recoverable from its signature alone, without reading the body. Agents parse signatures; they don't do whole-program analysis.

### P2: Incremental Adoption

The design must work with Zero's existing syntax and type system. No "start over" proposals. Every change must be implementable as a diff against the current compiler.

### P3: Honest Documentation

The docs should describe what the system *actually does*, not what it aspires to do. If World is a documentation convention, say so. If it's a real capability, enforce it.

### P4: Target-Aware

Zero compiles to multiple targets (darwin-arm64, linux-musl-x64, wasm32-wasi, wasm32-web). The capability system must work across all targets, with target-gating as a first-class concept.

## Proposed Design

### Layer 1: Effect Signatures (v0.2)

Replace the boolean `raises` with effect rows in function signatures.

**Current syntax:**
```zero
pub fun main(world: World) -> Void raises {
    check world.out.write("hello\n")
}
```

**Proposed syntax:**
```zero
pub fun main(world: World) -> Void raises<io> {
    check world.out.write("hello\n")
}
```

The effect row `<io>` is a comma-separated list of effect names. This is not a breaking change — bare `raises` remains valid and is equivalent to `raises<>` (open effects, inferred by the compiler).

**Effect names** are drawn from a fixed vocabulary:
- `io` — console I/O (world.out, world.err)
- `fs` — filesystem operations
- `net` — network operations
- `proc` — process spawning/management
- `env` — environment variable access
- `args` — command-line argument access
- `time` — time/clock access
- `rand` — random number generation
- `ai` — AI model calls
- `alloc` — heap allocation

**Compiler changes:**
- Parser: recognize `<effect1, effect2, ...>` after `raises`
- Checker: track effect sets per function (replace `bool raises` with `EffectSet raises`)
- Checker: propagate effect requirements through call chains
- Checker: emit new diagnostic `EFF001` for effect mismatches
- JSON: include `effects` array in function signatures in `graph --json`

**Example propagation:**
```zero
fun readConfig(path: String, fs: Fs) -> String raises<fs> {
    // fs operations are allowed because this function declares raises<fs>
}

fun main(world: World) -> Void raises<io, fs> {
    // Can call readConfig because our effect set includes <fs>
    let config = readConfig("config.txt", world.fs())
    check world.out.write(config)
}
```

**Backward compatibility:**
```zero
// Old style still works — effects are inferred
fun legacy() -> Void raises {
    // Compiler infers effects from stdlib calls
}
```

### Layer 2: Fine-Grained Capabilities (v0.3)

Split `World` into a hierarchy of capability types that can be passed independently.

**Current:**
```zero
pub fun main(world: World) -> Void raises {
    // world gives access to everything
}
```

**Proposed:**
```zero
pub fun main(world: World) -> Void raises<io> {
    // World is the root capability, but functions can request sub-capabilities
}

fun processFile(fs: Fs) -> Void raises<fs> {
    // Only has filesystem access, not network or process
}

fun main(world: World) -> Void raises<io, fs> {
    // Derive Fs from World
    processFile(world.fs())
}
```

**Capability type hierarchy:**
```
World (root)
  ├── Out (world.out) — console output
  ├── Err (world.err) — console error
  ├── Fs (world.fs()) — filesystem
  ├── Net (world.net()) — network
  ├── Env (world.env()) — environment
  ├── Args (world.args()) — CLI args
  ├── Clock (world.clock()) — time
  ├── Rand (world.rand()) — random
  └── Proc (world.proc()) — processes
```

**Key change:** `std.fs.host()` is removed. The only way to get `Fs` is from `world.fs()`. This makes capabilities unforgeable.

**Compiler changes:**
- Type checker: track capability parameters separately from regular parameters
- Type checker: verify that capability-derived functions receive capabilities from their caller
- Type checker: prevent capability types from being stored in globals or heap-allocated structures
- Parser: recognize `world.fs()`, `world.net()` etc. as capability derivation

### Layer 3: Substructural Discipline (v0.4)

Make capability types linear/affine — they cannot be duplicated or stored ambiently.

**Current (World is copyable):**
```zero
let w1 = world
let w2 = world  // Both are valid — World is just a reference
storeInGlobal(world)  // No error
```

**Proposed (World is affine):**
```zero
let w1 = world
let w2 = world  // Error: world has been moved
storeInGlobal(world)  // Error: cannot store capability in global
```

**Compiler changes:**
- Type checker: track capability ownership through move semantics
- Type checker: prevent capability types from being used in `shape` fields
- Type checker: prevent capability types from being returned from functions
- New diagnostic `CAP001`: "capability type cannot be duplicated"
- New diagnostic `CAP002`: "capability type cannot be stored in global/heap"

**Practical impact:** This is the most invasive change. It requires Zero's borrow checker to understand linearity for capability types specifically. This is why it's Layer 3, not Layer 1.

### Layer 4: Effect Handlers (v0.5+)

Add algebraic effect handlers, allowing users to define custom effects and handle them.

**Proposed syntax:**
```zero
effect Http {
    fun fetch(url: String) -> Response
}

fun handler(url: String) -> Response raises<Http> {
    // Uses the Http effect
}

fun main(world: World) -> Void raises<io> {
    // Handle the Http effect with a concrete implementation
    with Http handledBy {
        fun fetch(url: String) -> Response {
            // Concrete implementation using world.net()
        }
    } {
        let response = handler("https://example.com")
        check world.out.write(response.body)
    }
}
```

This is a longer-term goal. Layers 1-3 are sufficient for v0.2-v0.4.

## Agent-Specific Benefits

### Static Effect Reasoning

An agent reading a function signature can now determine exactly what effects it has:

```zero
// Agent knows: this function only reads files, no network, no process spawning
fun parseConfig(fs: Fs, path: String) -> Config raises<fs>

// Agent knows: this function makes AI calls with budget tracking
fun generateCode(ai: Ai, spec: String) -> String raises<ai>

// Agent knows: this function is pure, no effects
fun validate(input: String) -> Bool
```

### Capability-Based Sandboxing

Agents generating code for sandboxed environments can verify capability requirements statically:

```zero
// Agent knows: this function requires fs, which is not available on wasm32-wasi
// The compiler will reject this at compile time, not runtime
fun loadPlugin(fs: Fs, path: String) -> Plugin raises<fs>
```

### Effect-Preserving Composition

Agents can compose functions while tracking effect accumulation:

```zero
fun step1() -> Data raises<fs>
fun step2(data: Data) -> Result raises<net>
fun step3(result: Result) -> Output raises<io>

// Agent knows: the composition requires <fs, net, io>
fun pipeline() -> Output raises<fs, net, io> {
    let data = step1()
    let result = step2(data)
    step3(result)
}
```

### Machine-Readable Contracts

The `graph --json` output becomes a machine-readable contract:

```json
{
  "name": "parseConfig",
  "effects": ["fs"],
  "requiresCapabilities": ["Fs"],
  "effectSafety": "sandboxed",
  "approvalBoundary": false
}
```

## Implementation Phasing

### Phase 1: Effect Signatures (2-3 weeks)

**Scope:** Parser + checker changes for `raises<io, fs>` syntax.

**Files changed:**
- `native/zero-c/src/parser.c` — parse effect rows
- `native/zero-c/src/checker.c` — track effect sets, propagate through calls
- `native/zero-c/src/main.c` — output effects in graph JSON
- `docs-site/articles/language-reference.md` — document effect syntax
- `docs-site/articles/diagnostics.md` — document new EFF diagnostics

**Tests:**
- Effect parsing: `raises<>`, `raises<io>`, `raises<io, fs, net>`
- Effect propagation: callee effects must be subset of caller effects
- Effect mismatch: calling `raises<fs>` function from `raises<io>` context
- Backward compatibility: bare `raises` still works
- JSON output: effects array in graph JSON

### Phase 2: Fine-Grained Capabilities (3-4 weeks)

**Scope:** Split World into capability hierarchy, remove ambient authority.

**Files changed:**
- `native/zero-c/src/checker.c` — capability tracking, prevent ambient minting
- `native/zero-c/src/main.c` — update stdlib helper table
- `docs-site/articles/modules/fs.md` — remove `std.fs.host()` documentation
- `docs-site/articles/primitives.md` — update capability hierarchy
- All examples — update to use `world.fs()` instead of `std.fs.host()`

**Tests:**
- `world.fs()` returns `Fs` capability
- `std.fs.host()` is rejected (or removed)
- Capability cannot be stored in global
- Capability cannot be duplicated
- Target-gating still works (Fs rejected on wasm32-wasi)

### Phase 3: Substructural Discipline (4-6 weeks)

**Scope:** Linear types for capability tokens.

**Files changed:**
- `native/zero-c/src/checker.c` — linearity tracking for capability types
- `native/zero-c/src/ir.c` — IR changes for linear capability values
- `docs-site/articles/language-reference.md` — document capability linearity

**Tests:**
- Capability cannot be copied
- Capability cannot be stored in shape field
- Capability cannot be returned from function
- Capability can be moved (passed to callee)
- Borrowing (`&world`) creates a reference, not a copy

### Phase 4: Effect Handlers (Future)

**Scope:** User-defined effects and handlers.

This is a significant language feature that requires careful design. Defer until Layers 1-3 are stable.

## Relationship to Existing Work

- **Issue #72**: This design directly addresses all five structural concerns
- **Issue #4** (AI primitives): `std.ai` uses the effect system — `raises<ai>` makes AI effects explicit
- **PR #65** (AI primitives prototype): The `Ai` capability type fits into Layer 2's hierarchy
- **PR #49** (effectsSummary in graph JSON): Layer 1 extends this with per-function effect rows
- **Issue #20** (structured concurrency): Effect handlers (Layer 4) would enable structured concurrency as a library feature

## Open Questions

1. **Effect row syntax**: `raises<io, fs>` vs `raises { io, fs }` vs `raises io | fs`?
   - Recommendation: Angle brackets match generic syntax (`FixedVec<T, N>`), making it familiar.

2. **Effect inference**: Should bare `raises` infer the maximal effect set, or require explicit annotation?
   - Recommendation: Infer for private functions, require explicit for public API functions.

3. **Capability derivation**: Should `world.fs()` return a new `Fs` each time, or the same `Fs`?
   - Recommendation: Same `Fs` (World owns it, lends it out). This is simpler and sufficient for v0.

4. **Backward compatibility**: How long to support the old `std.fs.host()` pattern?
   - Recommendation: Deprecate in v0.2, remove in v0.4. Add a migration diagnostic.

5. **Effect granularity**: Is `io` too coarse? Should it be split into `stdout` and `stderr`?
   - Recommendation: Start coarse (`io`), split later if needed. Coarse is better for agents — fewer concepts to track.

## Appendix: Comparison with Other Languages

| Feature | Zero (current) | Zero (proposed) | Koka | Pony | E |
|---------|---------------|-----------------|------|------|---|
| Effect in signature | Boolean `raises` | Effect rows `raises<io, fs>` | Effect rows `<io, console>` | Capability in type `ref` | Guard in type |
| Effect propagation | Manual `check` | Automatic through call chain | Automatic | Automatic | Automatic |
| Capability minting | `std.fs.host()` (ambient) | `world.fs()` (derived) | N/A (effect-based) | `iso`, `trn`, `ref` | N/A |
| Substructural discipline | None | Affine (Layer 3) | None | Full (iso/trn/ref/val/box) | None |
| Effect handlers | None | Planned (Layer 4) | Built-in | N/A | N/A |
| Agent-readable effects | Partial (graph JSON) | Full (signature + JSON) | Partial | Partial | None |

Zero's proposed design is "Koka-lite with Pony-inspired capabilities" — simpler than either, but sufficient for agentic programming.
