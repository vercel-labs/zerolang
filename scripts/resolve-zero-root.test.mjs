import assert from "node:assert/strict";
import { describe, it } from "node:test";
import { defaultZeroRoot, resolveZeroRoot, zeroBinaryPath, zlsScriptPath } from "./resolve-zero-root.mjs";

describe("resolveZeroRoot", () => {
  it("finds the repository root from the workspace", async () => {
    const root = await resolveZeroRoot(defaultZeroRoot);
    assert.equal(root, defaultZeroRoot);
    assert.match(zlsScriptPath(root), /scripts\/zls\.mts$/);
    assert.match(zeroBinaryPath(root), /bin\/zero$/);
  });

  it("honors ZERO_ROOT when set", async () => {
    const previous = process.env.ZERO_ROOT;
    process.env.ZERO_ROOT = defaultZeroRoot;
    try {
      const root = await resolveZeroRoot("/tmp");
      assert.equal(root, defaultZeroRoot);
    } finally {
      if (previous === undefined) {
        delete process.env.ZERO_ROOT;
      } else {
        process.env.ZERO_ROOT = previous;
      }
    }
  });
});
