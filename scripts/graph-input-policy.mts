#!/usr/bin/env -S node --experimental-strip-types --disable-warning=ExperimentalWarning
import { readFileSync } from "node:fs";

const files = [
  "tests/zero-cli.test.ts",
  "scripts/snapshot-command-contracts.mts",
  "scripts/program-graph-parity.mts",
  "scripts/reliability-smoke.mts",
];

const compilerInputCommands = new Set(["check", "build", "run", "test", "size", "ship", "mem", "doc", "dev", "time", "fix"]);
const compilerInputValueFlags = new Set(["--backend", "--emit", "--filter", "--out", "--profile", "--release", "--target"]);
const abiInputSubcommands = new Set(["check", "dump"]);

function lineForOffset(text: string, offset: number) {
  return text.slice(0, offset).split("\n").length;
}

function findArrayEnd(text: string, start: number) {
  let depth = 0;
  let quote = "";
  let escaped = false;
  for (let i = start; i < text.length; i++) {
    const ch = text[i];
    if (quote) {
      if (escaped) {
        escaped = false;
      } else if (ch === "\\") {
        escaped = true;
      } else if (ch === quote) {
        quote = "";
      }
      continue;
    }
    if (ch === "\"" || ch === "'" || ch === "`") {
      quote = ch;
    } else if (ch === "[") {
      depth++;
    } else if (ch === "]") {
      depth--;
      if (depth === 0) return i + 1;
    }
  }
  return -1;
}

function stringLiterals(segment: string) {
  const out: string[] = [];
  const re = /"((?:\\.|[^"\\])*)"/g;
  let match;
  while ((match = re.exec(segment))) {
    out.push(match[1].replace(/\\"/g, "\"").replace(/\\\\/g, "\\"));
  }
  return out;
}

function isCompilerInputCommand(strings: string[]) {
  if (strings.length === 0) return false;
  if (compilerInputCommands.has(strings[0])) return true;
  return strings[0] === "abi" && abiInputSubcommands.has(strings[1]);
}

function projectionInputViolations(strings: string[]) {
  const violations: string[] = [];
  let skipNext = false;
  for (const value of strings.slice(1)) {
    if (value === "--") break;
    if (skipNext) {
      skipNext = false;
      continue;
    }
    if (compilerInputValueFlags.has(value)) {
      skipNext = true;
      continue;
    }
    if (value.endsWith(".0")) violations.push(value);
  }
  return violations;
}

const failures: string[] = [];
for (const file of files) {
  const text = readFileSync(file, "utf8");
  for (let i = 0; i < text.length; i++) {
    if (text[i] !== "[") continue;
    const end = findArrayEnd(text, i);
    if (end === -1) break;
    const segment = text.slice(i, end);
    const strings = stringLiterals(segment);
    if (isCompilerInputCommand(strings)) {
      for (const input of projectionInputViolations(strings)) {
        failures.push(`${file}:${lineForOffset(text, i)}: compiler command ${strings[0]} uses projection input ${input}`);
      }
    }
    i = end - 1;
  }
}

if (failures.length > 0) {
  console.error("direct .0 compiler inputs are not allowed in command contracts/tests:");
  for (const failure of failures) console.error(`- ${failure}`);
  process.exit(1);
}

console.log("graph input policy ok");
