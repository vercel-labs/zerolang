"use client";

import { useState } from "react";

const TABS = [
  {
    id: "humans",
    label: "For humans",
    command: "curl -fsSL https://zerolang.ai/install.sh | bash",
  },
  {
    id: "agents",
    label: "For agents",
    command: "npx skills add vercel-labs/zero",
  },
] as const;

type TabId = (typeof TABS)[number]["id"];

export function InstallCopy() {
  const [activeId, setActiveId] = useState<TabId>(TABS[0].id);
  const [copied, setCopied] = useState(false);

  const active = TABS.find((t) => t.id === activeId) ?? TABS[0];

  async function handleCopy() {
    try {
      await navigator.clipboard.writeText(active.command);
      setCopied(true);
      setTimeout(() => setCopied(false), 1400);
    } catch {}
  }

  return (
    <div className="mt-6 flex w-full max-w-[32rem] flex-col items-center gap-3">
      <div className="flex items-center gap-4 text-[0.9375rem]">
        {TABS.map((tab, i) => (
          <div key={tab.id} className="flex items-center gap-4">
            {i > 0 && <span className="h-4 w-px bg-border" />}
            <button
              type="button"
              onClick={() => {
                setActiveId(tab.id);
                setCopied(false);
              }}
              className={`cursor-pointer font-medium transition ${
                activeId === tab.id ? "text-fg" : "text-muted hover:text-fg"
              }`}
            >
              {tab.label}
            </button>
          </div>
        ))}
      </div>
      <div className="flex w-full items-center overflow-hidden rounded-md border border-border bg-surface">
        <code className="flex-1 overflow-x-auto whitespace-nowrap px-4 py-2 text-left font-mono text-[0.8125rem] tracking-tight text-muted">
          <span className="text-muted/60">$ </span>
          {active.command}
        </code>
        <button
          type="button"
          aria-label="Copy install command"
          onClick={handleCopy}
          className="flex h-10 w-10 shrink-0 cursor-pointer items-center justify-center self-stretch border-l border-border text-muted transition hover:bg-surface-muted hover:text-fg"
        >
          {copied ? (
            <svg width="14" height="14" viewBox="0 0 16 16" fill="currentColor" className="text-success">
              <path d="M13.78 3.22a.75.75 0 0 1 0 1.06l-7.25 7.25a.75.75 0 0 1-1.06 0L2.22 8.28a.75.75 0 0 1 1.06-1.06L6 9.94l6.72-6.72a.75.75 0 0 1 1.06 0Z" />
            </svg>
          ) : (
            <svg width="14" height="14" viewBox="0 0 16 16" fill="currentColor">
              <path d="M5 1.75A1.75 1.75 0 0 1 6.75 0h5.5A1.75 1.75 0 0 1 14 1.75v5.5A1.75 1.75 0 0 1 12.25 9H11V7.5h1.25a.25.25 0 0 0 .25-.25v-5.5a.25.25 0 0 0-.25-.25h-5.5a.25.25 0 0 0-.25.25V3H5V1.75Z" />
              <path d="M2 4.75A1.75 1.75 0 0 1 3.75 3h5.5A1.75 1.75 0 0 1 11 4.75v5.5A1.75 1.75 0 0 1 9.25 12h-5.5A1.75 1.75 0 0 1 2 10.25v-5.5Zm1.75-.25a.25.25 0 0 0-.25.25v5.5c0 .138.112.25.25.25h5.5a.25.25 0 0 0 .25-.25v-5.5a.25.25 0 0 0-.25-.25h-5.5Z" />
            </svg>
          )}
        </button>
      </div>
    </div>
  );
}
