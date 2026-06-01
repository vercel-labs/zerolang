import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { join, resolve } from "node:path";
import { describe, it } from "node:test";
import ts from "typescript";

const docsSiteRoot = resolve(import.meta.dirname, "../..");

function loadModule(relativePath) {
  const sourcePath = join(docsSiteRoot, relativePath);
  const source = readFileSync(sourcePath, "utf8");
  const { outputText } = ts.transpileModule(source, {
    compilerOptions: {
      module: ts.ModuleKind.CommonJS,
      target: ts.ScriptTarget.ES2022,
    },
    fileName: sourcePath,
  });
  const module = { exports: {} };
  new Function("exports", "module", outputText)(module.exports, module);
  return module.exports;
}

function makeReq(headers) {
  const map = new Map(Object.entries(headers).map(([k, v]) => [k.toLowerCase(), v]));
  return {
    headers: {
      get(name) {
        const value = map.get(name.toLowerCase());
        return value == null ? null : value;
      },
    },
  };
}

const { getRateLimitIdentifier } = loadModule("lib/rate-limit-identifier.ts");

describe("getRateLimitIdentifier", () => {
  it("prefers x-vercel-forwarded-for over everything else", () => {
    const req = makeReq({
      "x-vercel-forwarded-for": "203.0.113.10",
      "x-real-ip": "10.0.0.5",
      "x-forwarded-for": "1.1.1.1, 198.51.100.1",
    });
    assert.equal(getRateLimitIdentifier(req), "203.0.113.10");
  });

  it("falls back to x-real-ip when vercel header is absent", () => {
    const req = makeReq({
      "x-real-ip": "203.0.113.20",
      "x-forwarded-for": "1.1.1.1, 198.51.100.1",
    });
    assert.equal(getRateLimitIdentifier(req), "203.0.113.20");
  });

  it("uses the LAST x-forwarded-for hop, not the first", () => {
    // Vercel and most reverse proxies APPEND to X-Forwarded-For; the
    // rightmost value is what the trusted proxy added, the leftmost is
    // client-supplied and trivially spoofable. The legacy implementation
    // took split(',')[0], which let an attacker pick their own bucket
    // by sending `X-Forwarded-For: <anything>` per request.
    const req = makeReq({
      "x-forwarded-for": "1.1.1.1, 198.51.100.1, 203.0.113.30",
    });
    assert.equal(getRateLimitIdentifier(req), "203.0.113.30");
  });

  it("ignores empty entries in x-forwarded-for", () => {
    const req = makeReq({
      "x-forwarded-for": "1.1.1.1, ,",
    });
    assert.equal(getRateLimitIdentifier(req), "1.1.1.1");
  });

  it("returns 'anonymous' when no header is set", () => {
    const req = makeReq({});
    assert.equal(getRateLimitIdentifier(req), "anonymous");
  });

  it("returns 'anonymous' when every candidate header is blank", () => {
    const req = makeReq({
      "x-vercel-forwarded-for": "  ",
      "x-real-ip": "",
      "x-forwarded-for": ", , ",
    });
    assert.equal(getRateLimitIdentifier(req), "anonymous");
  });

  it("regression: a client-supplied X-Forwarded-For does NOT change the bucket", () => {
    // Same trusted-proxy IP, attacker sends two different leftmost spoofs.
    // Pre-fix, these produced two different identifiers (bypass). Post-fix,
    // both resolve to the trusted-proxy-appended rightmost value.
    const trustedHop = "203.0.113.40";

    const reqA = makeReq({
      "x-forwarded-for": `1.1.1.1, ${trustedHop}`,
    });
    const reqB = makeReq({
      "x-forwarded-for": `9.9.9.9, ${trustedHop}`,
    });

    assert.equal(getRateLimitIdentifier(reqA), trustedHop);
    assert.equal(getRateLimitIdentifier(reqB), trustedHop);
    assert.equal(getRateLimitIdentifier(reqA), getRateLimitIdentifier(reqB));
  });

  it("regression: same client cannot rotate buckets via x-vercel-forwarded-for absence", () => {
    // If somehow x-vercel-forwarded-for and x-real-ip are stripped, the
    // attacker can still only rotate the LEFTMOST of XFF, never the RIGHTMOST
    // (which the trusted proxy added). Two requests from the same trusted
    // proxy with different attacker-leftmost values must collide.
    const idA = getRateLimitIdentifier(
      makeReq({ "x-forwarded-for": "attacker-A, trusted-proxy" }),
    );
    const idB = getRateLimitIdentifier(
      makeReq({ "x-forwarded-for": "attacker-B, trusted-proxy" }),
    );
    assert.equal(idA, "trusted-proxy");
    assert.equal(idB, "trusted-proxy");
  });
});
