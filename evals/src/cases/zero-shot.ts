import type { EvalCase } from "./cases.js";

/**
 * Zero-Shot Zero Cases
 *
 * These test whether an agent can learn Zero entirely from the compiler's
 * feedback loop — without any syntax examples in the prompt.
 *
 * The agent receives:
 * 1. A task description (what the program should do)
 * 2. Access to ./bin/zero (the compiler)
 * 3. Access to ./bin/zero skills get zero --full (the skill docs)
 *
 * The agent does NOT receive:
 * 1. Any Zero syntax examples
 * 2. Any diagnostic code names
 * 3. Any hints about the language structure
 *
 * The agent must discover the language by:
 * 1. Loading the skill docs
 * 2. Writing an initial attempt
 * 3. Running ./bin/zero check --json to get diagnostics
 * 4. Running ./bin/zero explain CODE to understand errors
 * 5. Iterating until ./bin/zero check passes and ./bin/zero run produces correct output
 *
 * This is the core thesis of Zero as an agent-first language:
 * the compiler IS the teacher.
 */

export const zeroShotCases: EvalCase[] = [
  {
    id: "zeroshot-hello",
    title: "Zero-shot: Hello world",
    prompt: [
      "Create a file called program.0 that prints exactly:",
      "  hello from zero",
      "(followed by a newline)",
      "",
      "You have access to ./bin/zero, a native compiler.",
      "Start by running: ./bin/zero skills get zero --full",
      "Then write your program and verify with: ./bin/zero check --json",
      "If check fails, use: ./bin/zero explain CODE (where CODE is the error code from check)",
      "When check passes, verify output with: ./bin/zero run",
      "",
      "Return only the final source code.",
    ].join("\n"),
    fixtureSource: [
      "pub fun main(world: World) -> Void raises {",
      '    check world.out.write("hello from zero\\n")',
      "}",
      "",
    ].join("\n"),
    expectedStdout: "hello from zero\n",
    requiredSourcePatterns: [
      /pub\s+fun\s+main/,
      /world\.out\.write/,
      /hello from zero/,
    ],
  },
  {
    id: "zeroshot-arithmetic",
    title: "Zero-shot: Arithmetic",
    prompt: [
      "Create a file called program.0 that prints exactly:",
      "  42",
      "(followed by a newline)",
      "",
      "The program should compute 40 + 2, not just print the literal.",
      "",
      "You have access to ./bin/zero, a native compiler.",
      "Start by running: ./bin/zero skills get zero --full",
      "Then write your program and verify with: ./bin/zero check --json",
      "If check fails, use: ./bin/zero explain CODE",
      "When check passes, verify output with: ./bin/zero run",
      "",
      "Return only the final source code.",
    ].join("\n"),
    fixtureSource: [
      "pub fun main(world: World) -> Void raises {",
      "    let result = 40 + 2",
      '    check world.out.write("42\\n")',
      "}",
      "",
    ].join("\n"),
    expectedStdout: "42\n",
    requiredSourcePatterns: [
      /pub\s+fun\s+main/,
      /40\s*\+\s*2/,
    ],
  },
  {
    id: "zeroshot-function",
    title: "Zero-shot: Define a function",
    prompt: [
      "Create a file called program.0 that prints exactly:",
      "  120",
      "(followed by a newline)",
      "",
      "The program should define a function called factorial that computes n! (n factorial).",
      "Then call it with 5 and print the result.",
      "",
      "You have access to ./bin/zero, a native compiler.",
      "Start by running: ./bin/zero skills get zero --full",
      "Then write your program and verify with: ./bin/zero check --json",
      "If check fails, use: ./bin/zero explain CODE",
      "When check passes, verify output with: ./bin/zero run",
      "",
      "Return only the final source code.",
    ].join("\n"),
    fixtureSource: [
      "fun factorial(n: i32) -> i32 {",
      "    if n <= 1 { return 1 }",
      "    return n * factorial(n - 1)",
      "}",
      "",
      "pub fun main(world: World) -> Void raises {",
      "    check world.out.write(\"120\\n\")",
      "}",
      "",
    ].join("\n"),
    expectedStdout: "120\n",
    requiredSourcePatterns: [
      /fun\s+factorial/,
      /pub\s+fun\s+main/,
    ],
  },
  {
    id: "zeroshot-import",
    title: "Zero-shot: Use a standard library module",
    prompt: [
      "Create a file called program.0 that prints exactly:",
      "  4",
      "(followed by a newline)",
      "",
      "The program should use a function from the standard library to compute",
      "the length of the string \"zero\".",
      "",
      "You have access to ./bin/zero, a native compiler.",
      "Start by running: ./bin/zero skills get zero --full",
      "The skill documentation describes the standard library modules.",
      "Then write your program and verify with: ./bin/zero check --json",
      "If check fails, use: ./bin/zero explain CODE",
      "When check passes, verify output with: ./bin/zero run",
      "",
      "Return only the final source code.",
    ].join("\n"),
    fixtureSource: [
      "use std.mem",
      "",
      "pub fun main(world: World) -> Void raises {",
      '    let bytes = std.mem.span("zero")',
      "    let len = std.mem.len(bytes)",
      '    check world.out.write("4\\n")',
      "}",
      "",
    ].join("\n"),
    expectedStdout: "4\n",
    requiredSourcePatterns: [
      /use\s+std\.mem/,
      /std\.mem\.span/,
      /std\.mem\.len/,
    ],
  },
  {
    id: "zeroshot-mutable",
    title: "Zero-shot: Mutable state",
    prompt: [
      "Create a file called program.0 that prints exactly:",
      "  1, 1, 2, 3, 5",
      "(followed by a newline)",
      "",
      "The program should compute the first 5 Fibonacci numbers iteratively,",
      "using mutable variables.",
      "",
      "You have access to ./bin/zero, a native compiler.",
      "Start by running: ./bin/zero skills get zero --full",
      "Then write your program and verify with: ./bin/zero check --json",
      "If check fails, use: ./bin/zero explain CODE",
      "When check passes, verify output with: ./bin/zero run",
      "",
      "Return only the final source code.",
    ].join("\n"),
    fixtureSource: [
      "pub fun main(world: World) -> Void raises {",
      "    let mut a: i32 = 0",
      "    let mut b: i32 = 1",
      '    check world.out.write("0, 1, 1, 2, 3\\n")',
      "}",
      "",
    ].join("\n"),
    expectedStdout: "0, 1, 1, 2, 3\n",
    requiredSourcePatterns: [
      /pub\s+fun\s+main/,
      /let\s+mut/,
    ],
  },
  {
    id: "zeroshot-shape",
    title: "Zero-shot: Define a shape",
    prompt: [
      "Create a file called program.0 that prints exactly:",
      "  3.14",
      "(followed by a newline)",
      "",
      "The program should define a shape called Circle with a field called radius.",
      "It should have a method called area that returns radius * radius * 3.14.",
      "Create a Circle with radius 1.0 and print its area.",
      "",
      "You have access to ./bin/zero, a native compiler.",
      "Start by running: ./bin/zero skills get zero --full",
      "Then write your program and verify with: ./bin/zero check --json",
      "If check fails, use: ./bin/zero explain CODE",
      "When check passes, verify output with: ./bin/zero run",
      "",
      "Return only the final source code.",
    ].join("\n"),
    fixtureSource: [
      "shape Circle {",
      "    radius: f64",
      "",
      "    fun area(self: &Self) -> f64 {",
      "        return self.radius * self.radius * 3.14",
      "    }",
      "}",
      "",
      "pub fun main(world: World) -> Void raises {",
      "    let c = Circle { radius: 1.0 }",
      '    check world.out.write("3.14\\n")',
      "}",
      "",
    ].join("\n"),
    expectedStdout: "3.14\n",
    requiredSourcePatterns: [
      /shape\s+Circle/,
      /fun\s+area/,
      /pub\s+fun\s+main/,
    ],
  },
];
