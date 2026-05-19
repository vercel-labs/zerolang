import type { EvalCase } from "./cases.js";

/**
 * Zero-Shot Repair Cases
 *
 * The hardest test: the agent receives broken code with NO explanation of
 * what's wrong. It must discover the errors entirely from compiler feedback.
 *
 * The prompt does NOT mention:
 * - What kind of errors to expect
 * - What diagnostic codes mean
 * - What the fix should look like
 *
 * The agent must:
 * 1. Load the skill docs
 * 2. Read the broken code
 * 3. Run ./bin/zero check --json
 * 4. Interpret the diagnostics
 * 5. Use ./bin/zero explain CODE if needed
 * 6. Fix and verify
 */

export const zeroShotRepairCases: EvalCase[] = [
  {
    id: "zeroshot-repair-basic",
    title: "Zero-shot repair: Basic syntax errors",
    prompt: [
      "The file broken.0 does not compile.",
      "Fix it so it compiles and runs correctly.",
      "",
      "You have access to ./bin/zero, a native compiler.",
      "Start by running: ./bin/zero skills get zero --full",
      "Read the file, then use ./bin/zero check --json to see what's wrong.",
      "Use ./bin/zero explain CODE to understand any diagnostic codes.",
      "Fix all errors and verify with ./bin/zero check --json and ./bin/zero run.",
      "",
      "Return only the fixed source code.",
    ].join("\n"),
    fixtureSource: [
      "pub fun main(world: World) -> Void raises {",
      '    check world.out.write("hello\\n");',
      "}",
      "",
    ].join("\n"),
    expectedStdout: "hello\n",
    requiredSourcePatterns: [
      /pub\s+fun\s+main/,
      /world\.out\.write/,
      /hello/,
    ],
  },
  {
    id: "zeroshot-repair-types",
    title: "Zero-shot repair: Type errors",
    prompt: [
      "The file broken.0 does not compile.",
      "Fix it so it compiles and runs correctly.",
      "",
      "You have access to ./bin/zero, a native compiler.",
      "Start by running: ./bin/zero skills get zero --full",
      "Read the file, then use ./bin/zero check --json to see what's wrong.",
      "Use ./bin/zero explain CODE to understand any diagnostic codes.",
      "Fix all errors and verify with ./bin/zero check --json and ./bin/zero run.",
      "",
      "Return only the fixed source code.",
    ].join("\n"),
    fixtureSource: [
      "pub fun main(world: World) -> Void raises {",
      "    let mut buf: [8]u8 = [0, 0, 0, 0, 0, 0, 0, 0]",
      '    let input = std.mem.span("hello")',
      "    std.mem.copy(buf, input)",
      '    check world.out.write("copied\\n")',
      "}",
      "",
    ].join("\n"),
    expectedStdout: "copied\n",
    requiredSourcePatterns: [
      /pub\s+fun\s+main/,
      /std\.mem\.copy/,
    ],
  },
  {
    id: "zeroshot-repair-import",
    title: "Zero-shot repair: Import errors",
    prompt: [
      "The file broken.0 does not compile.",
      "Fix it so it compiles and runs correctly.",
      "",
      "You have access to ./bin/zero, a native compiler.",
      "Start by running: ./bin/zero skills get zero --full",
      "Read the file, then use ./bin/zero check --json to see what's wrong.",
      "Use ./bin/zero explain CODE to understand any diagnostic codes.",
      "Fix all errors and verify with ./bin/zero check --json and ./bin/zero run.",
      "",
      "Return only the fixed source code.",
    ].join("\n"),
    fixtureSource: [
      "use std.",
      "",
      "pub fun main(world: World) -> Void raises {",
      '    let bytes = std.mem.span("zero")',
      '    check world.out.write("ok\\n")',
      "}",
      "",
    ].join("\n"),
    expectedStdout: "ok\n",
    requiredSourcePatterns: [
      /use\s+std\.mem/,
      /pub\s+fun\s+main/,
      /std\.mem\.span/,
    ],
  },
];
