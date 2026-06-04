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

`llvm` is a known experimental backend family. It is explicit-only: it is not
the default backend, not release eligible, and not accepted by `zero ship`.
Direct emitters remain the supported release path. LLVM can emit deterministic
textual LLVM IR when selected with `--backend llvm --emit llvm-ir`. Native LLVM
executable artifacts are buildable only for supported host targets with a ready
clang toolchain. LLVM lowering currently supports scalar code, direct calls,
branches, loops, primitive fixed arrays, byte views, readonly strings, and
primitive `std.mem` helpers. Native LLVM object output, unsupported targets,
unsupported MIR constructs, and `zero ship --backend llvm` must report a
structured backend blocker; they must not fall back to direct emitters.

Textual LLVM IR artifacts that reference Zero runtime helpers must report that
dependency in `objectBackend.linking.targetLibraries` and
`objectBackend.linkerPlan.staticLibraries`. Emitting the `.ll` file still does
not compile or link the runtime object.

`zero size --backend llvm` is a metadata report, not a build fallback. It may
report LLVM target triple, optimization level, retained runtime/helper facts,
and direct-vs-LLVM comparison rows without writing a native artifact.

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

LLVM facts may claim textual IR emission and host executable output only when
Zero can build the selected artifact through the LLVM path for that target.
LLVM facts must also carry `backendLifecycle` so tools can distinguish explicit
experimental readiness from supported release eligibility.

## Fallback Policy

Backend fallback is never implicit:

- direct requests do not fall back to LLVM;
- LLVM requests do not fall back to direct;
- removed C backend flags do not act as a debug or compatibility path;
- graph and source entry points follow the same backend contract.
