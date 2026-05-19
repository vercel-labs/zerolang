import type { EvalCase } from "./cases.js";

export const evalCases: EvalCase[] = [
  {
    id: "hello-world",
    title: "Hello world",
    prompt: [
      "Write a single-file Zero program named hello.0.",
      "It should print exactly `hello from zero` followed by a newline.",
      "Return only the Zero source code.",
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
      /World/,
      /world\.out\.write/,
      /hello from zero/,
    ],
  },
];
