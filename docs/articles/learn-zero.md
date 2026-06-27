## The Human Model

Zerolang has two views of the same program:

- The graph is the program database. Agents inspect and patch it.
- The `.0` projection is readable text. Humans use it for review and rare
  manual edits.

When this page shows Zero syntax, it is showing projection syntax. The graph
contains the same declarations, types, calls, and edges as structured facts.

## Expected Usage

Ask for the behavior in normal language. The Zero skills tell the agent to use
the graph:

```json-render
{
  "messages": [
    {
      "role": "user",
      "text": "make a cli that adds two numbers"
    },
    {
      "role": "assistant",
      "text": "I’ll add the function, wire the CLI, and run a sample input."
    },
    {
      "role": "tools",
      "calls": [
        {
          "command": "zero query --fn main",
          "output": "main\n  check world.out.write \"hello\\n\""
        },
        {
          "command": "zero patch /tmp/add-cli.patch",
          "output": "program graph patch ok"
        },
        {
          "command": "zero run -- 40 2",
          "output": "42"
        }
      ]
    }
  ]
}
```

Under the hood, the agent gathers current compiler knowledge with `zero skills`,
inspects the package with `zero status` or `zero query`, patches the graph, then
runs `zero check`, `zero test`, or `zero run` only when useful for the task.

## A Minimal Program

```zero
pub fn main(world: World) -> Void raises {
    check world.out.write("hello\n")
}
```

Pieces visible in both graph and projection:

- `pub fn main` declares the entry point.
- `world: World` is an explicit capability parameter.
- `Void` means no useful return value.
- `raises` marks a fallible function.
- `check` propagates a fallible operation.
- `world.out.write(...)` writes through an explicit output capability.

Run the graph input:

```sh
zero run examples/hello.graph
```

## Values And Bindings

```zero
let name: String = "Ada"
var count: u32 = 0
count = count + 1
```

`let` is immutable. `var` is mutable. Public constants and declarations should
carry explicit types because the graph, diagnostics, and docs all benefit from
stable type facts.

## Functions

```zero
fn add(x: i32, y: i32) -> i32 {
    return x + y
}

test "add works" {
    expect (add(40, 2) == 42)
}
```

Agents should usually add this through patch operations such as `addFunction`,
`addParam`, `addReturnBinary`, and `addTest`, or through the row-based body DSL
when replacing a function or block body.

## Types

```zero
type Point {
    x: i32,
    y: i32,
}

enum Status {
    Pending,
    Ready,
}
```

Types are graph declarations. Projection snippets make them readable for
humans, but tools should inspect declaration nodes and symbol references.

## Control Flow

```zero
if ready {
    return 1
} else {
    return 0
}

while index < count {
    index = index + 1
}
```

Conditions must be `Bool`. Branch and loop bodies are blocks in the graph, so
agents can patch a specific block without replacing the entire function.

## Absence And Errors

```zero
let value: Maybe<u32> = std.args.parseU32(1)
if value.has {
    return value.value
}
return 0
```

`Maybe<T>` represents absence. Fallible functions use `raises`; `check`
propagates failure through explicit control flow rather than exceptions.

## Memory Views

```zero
let bytes: Span<u8> = std.mem.span("hello")
var scratch: [16]u8 = [0_u8; 16]
let copied: usize = std.mem.copy(scratch, bytes)
```

`Span<T>` borrows contiguous storage. `[N]T` is fixed storage. Standard library
helpers prefer caller-owned buffers so the graph can expose allocation and
ownership facts.

## Packages And Projections

A graph-first package has:

```text
zero.toml
zero.graph
src/main.0
```

`zero.graph` is the normal compile input. `src/main.0` is the readable
projection named by the package target for source maps and review.

Use:

```sh
zero export
zero import
zero verify-projection
```

Only use `export` or `import` when a human review or manual edit calls for it.

## What To Read Next

- Read **CLI Reference** for command groups and graph patch forms.
- Read **Primitives And Types** for the language pieces behind graph facts.
- Read **Standard Library** before asking an agent for CLI, HTTP, JSON, or
  filesystem programs.
- Read **Diagnostics And Repair** when an agent hits a compiler error.
