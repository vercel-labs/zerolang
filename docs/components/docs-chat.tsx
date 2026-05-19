"use client";

import { isValidElement, useCallback, useEffect, useRef, useState } from "react";
import type { ComponentPropsWithoutRef, ReactNode } from "react";
import { useChat } from "@ai-sdk/react";
import { DefaultChatTransport } from "ai";
import { Streamdown } from "streamdown";
import type {
  Components as StreamdownComponents,
  ExtraProps as StreamdownExtraProps,
} from "streamdown";
import type {
  DynamicToolUIPart,
  UIDataTypes,
  UIMessage,
  UIMessagePart,
  UITools,
  ToolUIPart,
} from "ai";
import Link from "next/link";
import { Sheet, SheetContent, SheetTitle } from "@/components/ui/sheet";
import { highlight } from "@/lib/highlight";

const STORAGE_KEY = "docs-chat-messages";
const transport = new DefaultChatTransport({ api: "/api/docs-chat" });

const TOOL_LABELS = {
  readFile: { label: "Reading", pastLabel: "Read", argKey: "path" },
  bash: { label: "Running", pastLabel: "Ran", argKey: "command" },
};

type ToolLabel = {
  label: string;
  pastLabel: string;
  argKey?: string;
};

type ToolDisplayPart = ToolUIPart<UITools> | DynamicToolUIPart;

function extractCodeText(children: ReactNode): string {
  if (children == null || children === false) return "";
  if (typeof children === "string") return children;
  if (Array.isArray(children)) return children.map(extractCodeText).join("");
  if (isValidElement<{ children?: ReactNode }>(children)) {
    return extractCodeText(children.props.children);
  }
  return String(children);
}

function ChatCodeBlock({
  children,
}: ComponentPropsWithoutRef<"pre"> & StreamdownExtraProps) {
  const codeEl = Array.isArray(children) ? children[0] : children;
  const className = isValidElement<{ className?: string }>(codeEl)
    ? codeEl.props.className ?? ""
    : "";
  const language = /language-(\w+)/.exec(className)?.[1] ?? "";
  const code = extractCodeText(
    isValidElement<{ children?: ReactNode }>(codeEl) ? codeEl.props.children : codeEl,
  ).replace(/\n$/, "");
  const html = highlight(code, language);
  return (
    <pre className="not-prose my-3 overflow-x-auto rounded-md border border-border bg-code-bg p-3 text-[0.8125rem] leading-relaxed text-code-fg">
      <code
        className={className}
        dangerouslySetInnerHTML={{ __html: html }}
      />
    </pre>
  );
}

const STREAMDOWN_COMPONENTS: StreamdownComponents = {
  pre: ChatCodeBlock,
};

function isToolPart(
  part: UIMessagePart<UIDataTypes, UITools>,
): part is ToolDisplayPart {
  return part.type.startsWith("tool-") || part.type === "dynamic-tool";
}

function getToolName(part: ToolDisplayPart): string {
  if (part.type === "dynamic-tool") return part.toolName ?? "tool";
  return part.type.replace(/^tool-/, "");
}

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === "object" && value !== null;
}

function ToolCallDisplay({ part }: { part: ToolDisplayPart }) {
  const toolName = getToolName(part);
  const config: ToolLabel = TOOL_LABELS[toolName as keyof typeof TOOL_LABELS] ?? {
    label: toolName,
    pastLabel: toolName,
  };
  const isDone = part.state === "output-available";
  const isError = part.state === "output-error";
  const isRunning = !isDone && !isError;
  const displayLabel = isRunning ? config.label : config.pastLabel;

  const args = isRecord(part.input) ? part.input : {};
  const argValue = config.argKey ? args[config.argKey] : undefined;
  const argPreview =
    argValue != null
      ? String(argValue)
          .replace(/^\/workspace\//, "/")
          .replace(/\.md$/, "")
          .replace(/\/index$/, "") || "/"
      : "";

  const docsLink = toolName === "readFile" && argPreview.startsWith("/") ? argPreview : null;

  const argEl = argPreview ? (
    docsLink ? (
      <Link href={docsLink} className="truncate underline underline-offset-2">
        {argPreview}
      </Link>
    ) : (
      <span className="truncate">{argPreview}</span>
    )
  ) : null;

  return (
    <div className="min-w-0 py-0.5 text-xs">
      <span
        className={`inline-flex min-w-0 max-w-full items-center gap-1 font-mono ${
          isRunning
            ? "animate-pulse text-neutral-500 dark:text-neutral-400"
            : "text-neutral-400 dark:text-neutral-500"
        }`}
      >
        <span className="shrink-0">{displayLabel}</span>
        {argEl}
        {isError && <span className="text-red-500">failed</span>}
      </span>
    </div>
  );
}

const SUGGESTIONS = [
  "What is Zero?",
  "How do I install it?",
  "What commands are available?",
  "How does cross-compilation work?",
  "Show me a hello world.",
];

export function DocsChat() {
  const [open, setOpen] = useState(false);
  const [input, setInput] = useState("");
  const messagesScrollRef = useRef<HTMLDivElement | null>(null);
  const inputRef = useRef<HTMLTextAreaElement | null>(null);
  const restoredRef = useRef(false);

  const { messages, sendMessage, status, setMessages, error } = useChat({ transport });

  const isLoading = status === "streaming" || status === "submitted";
  const showMessages = messages.length > 0 || !!error || isLoading;

  useEffect(() => {
    if (restoredRef.current) return;
    restoredRef.current = true;
    try {
      const stored = sessionStorage.getItem(STORAGE_KEY);
      if (stored) {
        const parsed = JSON.parse(stored);
        if (Array.isArray(parsed) && parsed.length > 0) setMessages(parsed as UIMessage[]);
      }
    } catch {}
  }, [setMessages]);

  useEffect(() => {
    if (!restoredRef.current || isLoading) return;
    if (messages.length === 0) {
      sessionStorage.removeItem(STORAGE_KEY);
      return;
    }
    try {
      sessionStorage.setItem(STORAGE_KEY, JSON.stringify(messages));
    } catch {}
  }, [messages, isLoading]);

  useEffect(() => {
    function onKey(e: globalThis.KeyboardEvent) {
      if (e.key === "i" && (e.metaKey || e.ctrlKey)) {
        e.preventDefault();
        setOpen((prev) => !prev);
      }
    }
    document.addEventListener("keydown", onKey);
    return () => document.removeEventListener("keydown", onKey);
  }, []);

  useEffect(() => {
    if (open) {
      const t = setTimeout(() => inputRef.current?.focus(), 200);
      return () => clearTimeout(t);
    }
  }, [open]);

  useEffect(() => {
    if (error) setOpen(true);
  }, [error]);

  useEffect(() => {
    const el = messagesScrollRef.current;
    if (!el) return;
    requestAnimationFrame(() => {
      el.scrollTop = el.scrollHeight;
    });
  }, [messages, error]);

  const handleSubmit = useCallback(
    (e?: { preventDefault: () => void }) => {
      e?.preventDefault();
      if (!input.trim() || isLoading) return;
      sendMessage({ text: input });
      setInput("");
    },
    [input, isLoading, sendMessage],
  );

  const handleClear = useCallback(() => {
    setMessages([]);
    sessionStorage.removeItem(STORAGE_KEY);
  }, [setMessages]);

  return (
    <>
      {!open && (
        <button
          onClick={() => setOpen(true)}
          className="fixed bottom-4 left-1/2 z-50 flex -translate-x-1/2 items-center gap-2 rounded-lg bg-neutral-900 px-4 py-2 text-sm font-medium text-white shadow-lg transition-opacity hover:opacity-90 dark:bg-neutral-100 dark:text-neutral-900 sm:left-auto sm:right-4 sm:translate-x-0"
          aria-label="Ask AI"
        >
          Ask AI
          <kbd className="hidden items-center gap-0.5 font-mono text-xs opacity-60 sm:inline-flex">
            <span>&#8984;</span>I
          </kbd>
        </button>
      )}

      <Sheet open={open} onOpenChange={setOpen}>
        <SheetContent
          side="right"
          showCloseButton={false}
          className="flex w-full max-w-md flex-col gap-0 p-0 sm:max-w-md"
        >
          <SheetTitle className="sr-only">AI Chat</SheetTitle>

          <div className="flex shrink-0 items-center justify-between border-b border-neutral-200 px-4 py-3 dark:border-neutral-800">
            <span className="text-sm font-medium text-neutral-900 dark:text-neutral-100">
              Zero Docs
            </span>
            <div className="flex items-center gap-3">
              {showMessages && (
                <button
                  onClick={handleClear}
                  className="text-xs text-neutral-500 transition-colors hover:text-neutral-900 dark:text-neutral-400 dark:hover:text-neutral-100"
                  aria-label="Clear conversation"
                >
                  Clear
                </button>
              )}
              <button
                onClick={() => setOpen(false)}
                className="text-neutral-500 transition-colors hover:text-neutral-900 dark:text-neutral-400 dark:hover:text-neutral-100"
                aria-label="Close panel"
              >
                <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
                  <line x1="18" y1="6" x2="6" y2="18" />
                  <line x1="6" y1="6" x2="18" y2="18" />
                </svg>
              </button>
            </div>
          </div>

          {showMessages ? (
            <div ref={messagesScrollRef} className="flex-1 space-y-4 overflow-y-auto p-4">
              {messages.map((message) => {
                const hasVisible = message.parts?.some(
                  (p) => (p.type === "text" && p.text.length > 0) || isToolPart(p),
                );
                if (!hasVisible) return null;
                return (
                  <div key={message.id}>
                    {message.role === "user" ? (
                      <div className="whitespace-pre-wrap text-sm leading-relaxed text-neutral-500 dark:text-neutral-400">
                        {message.parts.filter((p) => p.type === "text").map((p) => p.text).join("")}
                      </div>
                    ) : (
                      <div className="space-y-2">
                        {message.parts.map((part, i) => {
                          if (part.type === "text" && part.text) {
                            return (
                              <div
                                key={i}
                                className="docs-chat-content max-w-none text-sm leading-relaxed text-neutral-900 dark:text-neutral-100"
                              >
                                <Streamdown components={STREAMDOWN_COMPONENTS}>
                                {part.text}
                              </Streamdown>
                              </div>
                            );
                          }
                          if (isToolPart(part)) {
                            return <ToolCallDisplay key={part.toolCallId ?? i} part={part} />;
                          }
                          return null;
                        })}
                      </div>
                    )}
                  </div>
                );
              })}
              {error && (
                <div className="rounded-md bg-red-50 px-3 py-2 text-sm text-red-600/80 dark:bg-red-950/30 dark:text-red-400/80">
                  {(() => {
                    try {
                      const parsed = JSON.parse(error.message);
                      return parsed.message || parsed.error || error.message;
                    } catch {
                      return error.message || "Something went wrong. Please try again.";
                    }
                  })()}
                </div>
              )}
            </div>
          ) : (
            <div className="flex flex-1 flex-col">
              <div className="flex flex-wrap gap-2 p-4">
                {SUGGESTIONS.map((s) => (
                  <button
                    key={s}
                    type="button"
                    onClick={() => sendMessage({ text: s })}
                    className="rounded-full border border-neutral-200 bg-neutral-100 px-3 py-1.5 text-xs font-medium text-neutral-600 transition-colors hover:text-neutral-900 dark:border-neutral-700 dark:bg-neutral-800 dark:text-neutral-400 dark:hover:text-neutral-100"
                  >
                    {s}
                  </button>
                ))}
              </div>
            </div>
          )}

          <form
            onSubmit={handleSubmit}
            className="flex shrink-0 items-end gap-2 border-t border-neutral-200 px-4 py-3 dark:border-neutral-800"
          >
            <textarea
              ref={inputRef}
              value={input}
              onChange={(e) => {
                setInput(e.target.value);
                e.target.style.height = "auto";
                e.target.style.height = `${e.target.scrollHeight}px`;
              }}
              rows={1}
              enterKeyHint="send"
              placeholder="Ask a question..."
              onKeyDown={(e) => {
                if (e.key === "Enter" && !e.shiftKey) {
                  e.preventDefault();
                  handleSubmit(e);
                }
              }}
              className="max-h-32 flex-1 resize-none bg-transparent text-base leading-relaxed text-neutral-900 outline-none placeholder:text-neutral-400 disabled:opacity-50 dark:text-neutral-100 dark:placeholder:text-neutral-500 sm:text-sm"
            />
            <button
              type="submit"
              disabled={isLoading || !input.trim()}
              className="shrink-0 rounded-full bg-neutral-900 p-1.5 text-white transition-colors hover:bg-neutral-700 disabled:opacity-30 dark:bg-neutral-100 dark:text-neutral-900 dark:hover:bg-neutral-300"
              aria-label="Send message"
            >
              <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
                <line x1="12" y1="19" x2="12" y2="5" />
                <polyline points="5 12 12 5 19 12" />
              </svg>
            </button>
          </form>
        </SheetContent>
      </Sheet>
    </>
  );
}
