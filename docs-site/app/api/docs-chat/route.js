import { NextResponse } from "next/server";
import { convertToModelMessages, stepCountIs, streamText } from "ai";
import { createBashTool } from "bash-tool";
import { docs } from "@/lib/docs";
import { readArticleBySlug } from "@/lib/articles";
import {
  docsChatDailyRateLimit,
  docsChatMinuteRateLimit,
  isDocsChatRateLimitConfigured,
} from "@/lib/rate-limit";

export const maxDuration = 60;

const DEFAULT_MODEL = "anthropic/claude-haiku-4.5";

const SYSTEM_PROMPT = `You are a helpful documentation assistant for Zero, the programming language for agents: a systems language that compiles to tiny binaries and gives AI agents structured diagnostics.

GitHub repository: https://github.com/vercel-labs/zero
Documentation: https://zerolang.ai

You have access to the full Zero documentation via the bash and readFile tools. The docs are available as markdown files in the /workspace/ directory. The file layout matches each page's URL slug — e.g. /workspace/index.md is the home, /workspace/getting-started.md is /getting-started, /workspace/modules/parse.md is /modules/parse.

When answering questions:
- Use the bash tool to list files (ls /workspace/) or search for content (grep -r "keyword" /workspace/)
- Use the readFile tool to read specific documentation pages (e.g. readFile with path "/workspace/getting-started.md")
- Do NOT use bash to write, create, modify, or delete files (no tee, cat >, sed -i, echo >, cp, mv, rm, mkdir, touch, etc.) — you are read-only
- Always base your answers on the actual documentation content
- Be concise and accurate
- If the docs don't cover a topic, say so honestly
- Do NOT include source references or file paths in your response
- Do NOT use emojis in your responses
- When showing Zero source code, use \`\`\`zero fenced blocks. When showing shell commands, use \`\`\`sh.`;

let cachedDocsFiles = null;

async function loadDocsFiles() {
  if (cachedDocsFiles) return cachedDocsFiles;

  const files = {};
  const results = await Promise.allSettled(
    docs.map(async (doc) => {
      const md = await readArticleBySlug(doc.slug);
      if (!md) return null;
      const fileName = doc.path === "/" ? "/index.md" : `${doc.path}.md`;
      return { fileName: fileName.replace(/^\//, ""), md };
    }),
  );
  for (const result of results) {
    if (result.status === "fulfilled" && result.value) {
      files[result.value.fileName] = result.value.md;
    }
  }
  cachedDocsFiles = files;
  return files;
}

function addCacheControl(messages) {
  if (messages.length === 0) return messages;
  return messages.map((message, index) => {
    if (index === messages.length - 1) {
      return {
        ...message,
        providerOptions: {
          ...message.providerOptions,
          anthropic: { cacheControl: { type: "ephemeral" } },
        },
      };
    }
    return message;
  });
}

function getRateLimitIdentifier(req) {
  const forwardedFor = req.headers.get("x-forwarded-for");
  const realIp = req.headers.get("x-real-ip");
  return forwardedFor?.split(",")[0]?.trim() || realIp?.trim() || "anonymous";
}

async function rateLimitRequest(req) {
  if (!isDocsChatRateLimitConfigured()) {
    return NextResponse.json(
      {
        error: "Chat rate limiting is not configured",
        message: "Set KV_REST_API_URL and KV_REST_API_TOKEN to enable docs chat.",
      },
      { status: 503 },
    );
  }

  const identifier = getRateLimitIdentifier(req);
  let minuteResult;
  let dailyResult;
  try {
    [minuteResult, dailyResult] = await Promise.all([
      docsChatMinuteRateLimit.limit(identifier),
      docsChatDailyRateLimit.limit(identifier),
    ]);
  } catch (error) {
    console.error("Docs chat rate limiting failed", error);
    return NextResponse.json(
      {
        error: "Chat rate limiting unavailable",
        message: "Docs chat is temporarily unavailable because rate limiting could not be applied.",
      },
      { status: 503 },
    );
  }

  if (minuteResult.success && dailyResult.success) return null;

  const isMinuteLimit = !minuteResult.success;
  return NextResponse.json(
    {
      error: "Rate limit exceeded",
      message: isMinuteLimit
        ? "Too many requests. Please wait a moment before trying again."
        : "Daily limit reached. Please try again tomorrow.",
    },
    { status: 429 },
  );
}

export async function POST(req) {
  if (!process.env.AI_GATEWAY_API_KEY && !process.env.VERCEL_OIDC_TOKEN) {
    return NextResponse.json(
      { error: "Chat is not configured. Set AI_GATEWAY_API_KEY or configure deployment OIDC." },
      { status: 503 },
    );
  }

  const rateLimitResponse = await rateLimitRequest(req);
  if (rateLimitResponse) return rateLimitResponse;

  let body;
  try {
    body = await req.json();
  } catch {
    return NextResponse.json({ error: "Invalid JSON body" }, { status: 400 });
  }

  const messages = body?.messages ?? [];

  const docsFiles = await loadDocsFiles();
  const {
    tools: { bash, readFile },
  } = await createBashTool({ files: docsFiles });

  const result = streamText({
    model: DEFAULT_MODEL,
    system: SYSTEM_PROMPT,
    messages: await convertToModelMessages(messages),
    stopWhen: stepCountIs(5),
    tools: { bash, readFile },
    prepareStep: ({ messages: stepMessages }) => ({
      messages: addCacheControl(stepMessages),
    }),
  });

  return result.toUIMessageStreamResponse();
}
