# Backend Contract

Zero backend selection is a compiler contract below the typed program and MIR.
The parser, checker, ProgramGraph, canonical source, and semantic facts must not
depend on a backend family.

## Selection

Backend selection has these dimensions:

- target: a supported Zero target name such as `linux-musl-x64`;
- backend family: `direct` or `llvm`;
- backend emitter: an implementation-specific emitter such as `zero-elf64`;
- artifact kind: `exe`, `obj`, or `llvm-ir`.

`direct` is the default backend family. A missing `--backend` means `direct`.
`--backend direct` also selects the direct family without selecting a specific
direct emitter. Direct emitter names such as `zero-elf64` remain exact direct
backend requests.

`llvm` is a known backend family. It is not buildable until an LLVM emitter is
implemented. Explicit LLVM requests must report a structured backend blocker;
they must not fall back to direct emitters.

Unknown backend names are command errors. Known-but-unavailable backend names
are buildability errors.

## MIR Input

Backends consume Zero MIR. They do not lower from source tokens, parser trees,
checker scopes, or generated textual views. MIR input must carry enough facts
for backend diagnostics to explain the unsupported construct, target, object
format, selected backend, and failing stage.

MIR verification remains backend-independent. Backend-specific buildability
checks may reject a verified MIR program when the selected backend cannot lower
the selected feature or artifact kind.

## Readiness

Target readiness answers whether the selected target, backend family, emitter,
artifact kind, and MIR subset are buildable.

Readiness JSON must include:

- `target`;
- `emit`;
- `objectFormat`;
- `backend`;
- `stage`;
- `languageOk`;
- `buildable`;
- structured diagnostics with optional `backendBlocker`.

The `backendBlocker` fields are:

- `target`;
- `objectFormat`;
- `backend`;
- `stage`;
- `unsupportedFeature`.

## Diagnostic Stages

Backend diagnostics distinguish these stages:

- `backend-selection`: the backend family is known but unavailable;
- `target-selection`: the backend family does not support the target;
- `lower`: MIR contains a feature the backend cannot lower;
- `buildability`: the backend cannot build the selected artifact kind or entry
  shape;
- `toolchain`: an external backend toolchain is required but missing;
- `emit`: a backend invariant failed after buildability accepted the program.

`BLD002` is used for unknown backend names. `BLD004` is used for ordinary
backend blockers. Code generation invariant failures remain `CGEN004`.

## Target Facts

`zero targets` exposes backend families separately from direct emitter facts.
`directBackend` remains the detailed direct-emitter record. `backendFamilies`
reports the default family, known families, currently available families, and
the no-fallback policy.

LLVM facts must not claim target support until Zero can emit and build the
selected artifact through the LLVM path for that target.

## Fallback Policy

Backend fallback is never implicit:

- direct requests do not fall back to LLVM;
- LLVM requests do not fall back to direct;
- removed C backend flags do not act as a debug or compatibility path;
- graph and source entry points follow the same backend contract.

