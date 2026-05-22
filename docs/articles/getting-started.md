## Getting Started

Zero is the programming language for agents. The fastest way to try it today is
to install the latest compiler release and run a small program.

## Install The Compiler

```sh
curl -fsSL https://zerolang.ai/install.sh | bash
export PATH="$HOME/.zero/bin:$PATH"
zero --version
```

The installer downloads the latest matching binary from the GitHub release and
writes it to `$HOME/.zero/bin/zero`.

## Check Your First File

Create `hello.0`:

```zero
pub fn main Void world World !
  check world.out.write "hello from zero\n"
```

Run the checker:

```sh
zero check hello.0
```

The important parts are:

- `pub fn main` declares the program entry point.
- `world World` is the capability object passed to the program by the runtime.
- `world.out.write ...` writes through that explicit capability.
- `check` handles a fallible operation.
- `!` marks that `main` can return an error.

Zero makes effects visible. A program that writes output asks for `World`
instead of reading a hidden global process object.

## Build And Run An Executable

Create `add.0`:

```zero
fn answer i32
  ret + 40 2

pub fn main Void world World !
  let value answer()
  if == value 42
    check world.out.write "math works\n"
  else
    check world.out.write "math broke\n"
```

Run it:

```sh
zero run add.0
```

Expected output:

```text
math works
```

This example introduces a helper function, a local binding, and `if` / `else`.

## Create A Package

The project workflow starts with `zero new`:

```sh
zero new cli hello
cd hello
zero check .
zero test .
zero run .
zero build --target linux-musl-x64 --out .zero/out/hello .
```

Single files are useful for learning, but real Zero projects use a `zero.json`
manifest and source files under `src/`.

## Learn The Core Syntax

Work through these examples in order:

```sh
zero check examples/hello.0
zero check examples/hello-let.0
zero check examples/functions.0
zero check examples/branch.0
zero check examples/point.0
zero check examples/result-choice.0
```

They cover:

- entry points and output
- `let` bindings
- functions and return values
- conditionals
- `type` data declarations
- `enum`, `choice`, and `match`

## Inspect A Package

The CLI package example lives in `examples/systems-package`:

```text
examples/
  systems-package/
    src/
      main.0
      helpers.0
      types.0
    zero.json
```

Check it:

```sh
zero check examples/systems-package
```

Inspect its module graph:

```sh
zero graph --json examples/systems-package
```

The manifest tells Zero where the entry point lives:

```json
{
  "package": { "name": "systems-package", "version": "0.1.0" },
  "targets": { "cli": { "kind": "exe", "main": "src/main.0" } }
}
```

## Next Steps

- Read Learn Zero for a practical language tour.
- Use the examples index to pick the next example by concept.
- Use Building From Source when you want to validate a local checkout.
- Use the language reference once you have written a few small programs.
