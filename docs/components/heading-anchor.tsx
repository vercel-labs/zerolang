"use client";

import type { MouseEvent } from "react";
import { useRef, useState } from "react";

export function HeadingAnchor({ id }: { id?: string }) {
  const [copied, setCopied] = useState(false);
  const timer = useRef<ReturnType<typeof setTimeout> | null>(null);

  if (!id) return null;

  async function handleClick(event: MouseEvent<HTMLAnchorElement>) {
    event.preventDefault();
    const url = `${window.location.origin}${window.location.pathname}#${id}`;
    try {
      await navigator.clipboard.writeText(url);
      setCopied(true);
    } catch {}
    if (timer.current) clearTimeout(timer.current);
    timer.current = setTimeout(() => setCopied(false), 1400);
  }

  return (
    <a
      href={`#${id}`}
      onClick={handleClick}
      aria-label={copied ? "Anchor link copied" : "Copy anchor link"}
      className="heading-anchor ml-2 inline-flex h-5 w-5 items-center justify-center align-middle text-muted no-underline opacity-0 transition-opacity hover:text-fg group-hover:opacity-100 focus-visible:opacity-100"
    >
      {copied ? (
        <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round">
          <polyline points="20 6 9 17 4 12" />
        </svg>
      ) : (
        <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
          <path d="M10 13a5 5 0 0 0 7.54.54l3-3a5 5 0 0 0-7.07-7.07l-1.72 1.71" />
          <path d="M14 11a5 5 0 0 0-7.54-.54l-3 3a5 5 0 0 0 7.07 7.07l1.71-1.71" />
        </svg>
      )}
    </a>
  );
}
