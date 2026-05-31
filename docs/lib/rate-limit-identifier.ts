/**
 * Picks a stable identifier for per-client rate limiting.
 *
 * `x-forwarded-for` is *appended* by trusted proxies (Vercel, most reverse
 * proxies) rather than overwritten, so its leftmost value is whatever the
 * client supplied and the rightmost is the most-recently-appended trusted
 * hop. Taking the leftmost lets any client send
 * `X-Forwarded-For: <random>` per request and bypass per-IP rate limits.
 *
 * Order of preference:
 *   1. `x-vercel-forwarded-for` — Vercel-edge-set, single client IP, not
 *      forwardable by clients.
 *   2. `x-real-ip` — typically stripped/overwritten by reverse proxies.
 *   3. `x-forwarded-for` last value — the most-recently-appended hop. The
 *      leftmost is client-supplied; the rightmost was added by the trusted
 *      proxy in front of this app, so a client cannot inject past it.
 *   4. `"anonymous"` — share a quota across unidentified callers rather than
 *      give each one a private bucket.
 */
export function getRateLimitIdentifier(req: Pick<Request, "headers">): string {
  const vercelForwarded = req.headers.get("x-vercel-forwarded-for")?.trim();
  if (vercelForwarded) return vercelForwarded;

  const realIp = req.headers.get("x-real-ip")?.trim();
  if (realIp) return realIp;

  const forwardedFor = req.headers.get("x-forwarded-for");
  if (forwardedFor) {
    const parts = forwardedFor.split(",").map((s) => s.trim()).filter(Boolean);
    const last = parts[parts.length - 1];
    if (last) return last;
  }

  return "anonymous";
}
