import { Ratelimit } from "@upstash/ratelimit";
import { Redis } from "@upstash/redis";

let minuteRateLimit: Ratelimit | null = null;
let dailyRateLimit: Ratelimit | null = null;

export function isDocsChatRateLimitConfigured() {
  return Boolean(process.env.KV_REST_API_URL && process.env.KV_REST_API_TOKEN);
}

function getRedis() {
  const url = process.env.KV_REST_API_URL;
  const token = process.env.KV_REST_API_TOKEN;

  if (!url || !token) {
    throw new Error("Docs chat rate limiting requires KV_REST_API_URL and KV_REST_API_TOKEN");
  }

  return new Redis({ url, token });
}

function readPositiveInt(name: string, fallback: number): number {
  const value = Number(process.env[name]);
  return Number.isInteger(value) && value > 0 ? value : fallback;
}

const MINUTE_LIMIT = readPositiveInt("RATE_LIMIT_PER_MINUTE", 10);
const DAILY_LIMIT = readPositiveInt("RATE_LIMIT_PER_DAY", 100);

export const docsChatMinuteRateLimit = {
  limit: async (identifier: string) => {
    if (!minuteRateLimit) {
      const redis = getRedis();
      minuteRateLimit = new Ratelimit({
        redis,
        limiter: Ratelimit.slidingWindow(MINUTE_LIMIT, "1 m"),
        prefix: "ratelimit:docs-chat:minute",
      });
    }
    return minuteRateLimit.limit(identifier);
  },
};

export const docsChatDailyRateLimit = {
  limit: async (identifier: string) => {
    if (!dailyRateLimit) {
      const redis = getRedis();
      dailyRateLimit = new Ratelimit({
        redis,
        limiter: Ratelimit.fixedWindow(DAILY_LIMIT, "1 d"),
        prefix: "ratelimit:docs-chat:daily",
      });
    }
    return dailyRateLimit.limit(identifier);
  },
};
