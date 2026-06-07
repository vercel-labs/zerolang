#!/usr/bin/env -S node --experimental-strip-types --disable-warning=ExperimentalWarning
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";

const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const zeroBin = path.join(repoRoot, "bin/zero");
const stdRoot = path.join(repoRoot, "std");
const tmpRoot = path.join(os.tmpdir(), `zero-stdlib-binary-graphs-${process.pid}`);

function runZero(args: string[]) {
  const result = spawnSync(zeroBin, args, {
    cwd: repoRoot,
    encoding: "utf8",
    stdio: ["ignore", "pipe", "pipe"],
  });
  if (result.status !== 0) {
    const command = ["bin/zero", ...args].join(" ");
    const output = `${result.stdout || ""}${result.stderr || ""}`.trim();
    throw new Error(`${command} failed\n${output}`);
  }
}

fs.mkdirSync(tmpRoot, { recursive: true });

const graphs = fs.readdirSync(stdRoot)
  .filter((name) => name.endsWith(".graph"))
  .sort((a, b) => a.localeCompare(b));

for (const name of graphs) {
  const relative = `std/${name}`;
  const tmp = path.join(tmpRoot, name);
  runZero(["validate", "--format", "binary", "--out", tmp, relative]);
  const magic = fs.readFileSync(tmp).subarray(0, 8).toString("latin1");
  if (magic !== "ZRGBIN1\0") throw new Error(`${relative}: generated graph is not a binary graph store`);
  fs.renameSync(tmp, path.join(repoRoot, relative));
}

fs.rmSync(tmpRoot, { recursive: true, force: true });
console.log(`converted ${graphs.length} std graph store(s) to binary`);
