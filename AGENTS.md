# Contributor Notes

Zero is a pre-1 experiment in building an agent-first programming language.
Keep public-facing changes honest about what works today without weakening that
positioning.

## Project Direction

Zero is still being shaped around the needs of agents. Breaking changes are
acceptable when they move the language, standard library, compiler, or tooling
closer to that goal.

Do not preserve legacy behavior by default. Prefer the clearer agent-facing
design over compatibility shims, migration layers, or carrying old paths
forward. Keep examples, docs, tests, and command contracts aligned with the new
behavior so the repository describes one coherent current system.

This does not mean broad churn for its own sake. Make direct changes that
advance Zero's agent-first goals: on-the-fly learnability, deterministic
inspection and repair, strong standard-library coverage, exceptional developer
experience, and regular patterns over syntactic convenience.

## Safety Expectations

Security vulnerabilities should be expected. Zero is not ready for production
systems, sensitive data, or trusted infrastructure.

Run and develop Zero in safe environments: isolated workspaces, disposable
inputs, and systems where compiler crashes, malformed output, or unsafe runtime
behavior cannot damage production state. Treat generated artifacts and examples
as experimental unless they have been reviewed for the specific environment
where they will run.

## Development

- Build the local compiler with `make -C native/zero-c`.
- Use `bin/zero` for focused checks; it execs the local native compiler at
  `.zero/bin/zero`.
- Keep examples runnable and docs copyable.
- Prefer small, direct changes over broad refactors.
- Use direct emitters for compiler output.

## Useful Checks

```sh
pnpm run docs:test
pnpm run conformance
pnpm run native:test
pnpm run command-contracts
```

For focused compiler work:

```sh
bin/zero check --json <file-or-package>
bin/zero graph --json <file-or-package>
bin/zero size --json <file-or-package>
bin/zero explain <diagnostic-code>
bin/zero fix --plan --json <file-or-package>
```

## Project Layout

- `native/zero-c/`: native compiler implementation.
- `examples/`: small runnable programs and packages.
- `conformance/`: language and CLI fixtures.
- `docs/`: public documentation site.
- `scripts/`: validation and release support tooling.

## Public Docs Policy

Docs should describe current user-facing behavior, not internal development
history. Avoid release-planning language, validation-report narratives, and
implementation diary details in pages intended for external readers.

## Releasing

Releases are manual, single-branch affairs. The maintainer controls the
changelog voice and format.

To prepare a release:

1. Create a release branch, such as `ctate/v0.1.1`.
2. Bump the release version in `package.json`, `docs/package.json`,
   `extensions/vscode/package.json`, and `native/zero-c/src/main.c`.
3. Update command-contract expectations that assert the compiler version.
4. Write the `CHANGELOG.md` entry for the new version, wrapped in
   `<!-- release:start -->` and `<!-- release:end -->` markers.
5. Remove the release markers from the previous release entry. Only the latest
   release entry should have markers.
6. Include a `### Contributors` section in the marked changelog entry. Derive
   contributors from commit authors and `Co-authored-by` trailers since the
   previous tag.
7. Run focused checks before opening or updating the PR:

```sh
make -C native/zero-c
bin/zero --version --json
pnpm run test:zero
pnpm run command-contracts:local
pnpm run docs:test
```

The release workflow reads the version from `package.json`, builds release
assets, and uses the content between the changelog markers as the GitHub
release body.
