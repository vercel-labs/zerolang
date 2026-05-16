# Zero

Zero is the programming language for agents: a systems language for small native tools, explicit effects, predictable memory, and structured compiler output.

Zero is experimental and still changing. The compiler, standard library, docs, and examples are useful for trying the language and giving feedback, but the language is not stable yet.

## Quick Start

Install the latest release:

```bash
curl -fsSL https://zerolang.ai/install.sh | bash
export PATH="$HOME/.zero/bin:$PATH"
zero --version
```

Check a program:

```bash
zero check examples/hello.0
```

Run a small executable:

```bash
zero run examples/add.0
```

Expected output:

```text
math works
```

## Learn Zero

- `docs-site/articles/getting-started.md`: build the compiler and run a first program.
- `docs-site/articles/learn-zero.md`: a practical tour of the language.
- `docs-site/articles/language-reference.md`: syntax and behavior reference.
- `examples/README.md`: examples grouped by concept.

Run the docs site locally:

```bash
npm run docs:dev
```

## Common Commands

```bash
zero check examples/hello.0
zero run examples/add.0
zero build --emit exe --target linux-musl-x64 examples/add.0 --out .zero/out/add
zero graph --json examples/systems-package
zero size --json examples/point.0
zero routes --json examples/web/hello
zero skills get zero --full
zero doctor --json
```

## Validation

```bash
npm run docs:test
npm run conformance
npm run native:test
npm run command-contracts
```

Benchmarks run locally by default:

```bash
npm run bench
```

## Repository Layout

- `native/zero-c/`: native compiler implementation.
- `compiler-zero/`: Zero-authored compiler sources.
- `examples/`: runnable Zero source examples.
- `conformance/`: language and CLI behavior fixtures.
- `docs-site/`: documentation site.
- `tests/`: TypeScript tests for CLI behavior.
- `extensions/vscode/`: editor syntax highlighting for `.0` files.
