import { access } from "node:fs/promises";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

async function exists(path) {
  try {
    await access(path);
    return true;
  } catch {
    return false;
  }
}

export async function resolveZeroRoot(startDir) {
  if (process.env.ZERO_ROOT) {
    return resolve(process.env.ZERO_ROOT);
  }

  let dir = resolve(startDir);
  for (let depth = 0; depth < 8; depth++) {
    const zeroBinary = join(dir, "bin/zero");
    const zlsScript = join(dir, "scripts/zls.mts");
    if ((await exists(zeroBinary)) && (await exists(zlsScript))) {
      return dir;
    }
    const parent = dirname(dir);
    if (parent === dir) break;
    dir = parent;
  }

  return resolve(startDir);
}

export function zlsScriptPath(zeroRoot) {
  return join(zeroRoot, "scripts/zls.mts");
}

export function zeroBinaryPath(zeroRoot) {
  return join(zeroRoot, "bin/zero");
}

export const defaultZeroRoot = resolve(dirname(fileURLToPath(import.meta.url)), "..");
