---
name: diagnostics
description: Read Zero diagnostics, explanations, and typed fix plans.
---

# Zero Diagnostics

Use this when Zero code fails to parse, typecheck, build, test, or target-check. Zero diagnostics are intended for agents: consume JSON first, then prose.

## Commands

```sh
zero check --json <input>
zero explain <diagnostic-code>
zero explain --json <diagnostic-code>
zero fix --plan --json <input>
```

`zero fix` is plan-only in this compiler. It reports candidate repairs but does not edit files.

## Diagnostic Shape

Important fields from `zero check --json`:

- `code`: stable diagnostic code such as `NAM003` or `TAR002`
- `message`: short human summary
- `path`, `line`, `column`, `length`: source span
- `expected` and `actual`: structured mismatch facts when available
- `help`: concise next action
- `fixSafety`: safety label for an agent repair
- `repair`: optional repair id and summary
- `related`: extra spans or facts

Do not scrape terminal prose when JSON is available.

## Fix Safety

`zero fix --plan --json` reports `safetyLevels` and per-fix `safety`:

- `format-only`: formatting or trivia only
- `behavior-preserving`: intended not to change runtime behavior
- `api-changing`: signatures, exports, or call sites may change
- `target-changing`: target support or capability use may change
- `requires-human-review`: the compiler cannot prove the edit is safe

Apply only the edit you can justify from the source and fix plan. Treat `requires-human-review` as a planning hint, not an automatic patch.

## Common Codes

- `NAM003`: unknown name; declare it, import it, or fix spelling.
- `IMP001`: unknown package-local import.
- `IMP002`: package-local import cycle.
- `PKG001`: local dependency path lacks `zero.json`.
- `PKG002`: package dependency cycle.
- `PKG003`: one package name resolves to conflicting versions.
- `PKG004`: selected target is not supported by a dependency.
- `TAR001`: unknown target; inspect `zero targets`.
- `TAR002`: capability unavailable for selected target.
- `BLD003`: removed backend flag; use direct emitters.

## Agent Triage

1. Run the failing command with `--json` when supported.
2. Use the span to inspect only the relevant source first.
3. Run `zero explain <code>` before broad refactors.
4. If multiple diagnostics share a root cause, fix the earliest source issue.
5. Re-run the same command after the patch.
