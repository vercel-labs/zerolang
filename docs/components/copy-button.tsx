"use client";

import type { MouseEvent } from "react";
import { useRef, useState } from "react";

export function CopyCodeButton() {
  const [state, setState] = useState("idle");
  const timer = useRef<ReturnType<typeof setTimeout> | null>(null);

  async function handleClick(event: MouseEvent<HTMLButtonElement>) {
    const button = event.currentTarget;
    const block = button.closest("figure[data-rehype-pretty-code-figure], div[data-code-block]");
    const code = block?.querySelector("code");
    const text = code?.textContent ?? "";
    if (!text) return;

    try {
      await navigator.clipboard.writeText(text);
      setState("copied");
    } catch {
      setState("failed");
    }

    if (timer.current) clearTimeout(timer.current);
    timer.current = setTimeout(() => setState("idle"), 1400);
  }

  const color =
    state === "copied" ? "text-success" : state === "failed" ? "text-danger" : "text-muted";

  return (
    <button
      type="button"
      aria-label={state === "copied" ? "Code copied to clipboard" : "Copy code to clipboard"}
      onClick={handleClick}
      className={`absolute right-2 top-2 z-10 inline-flex h-7 w-7 cursor-pointer items-center justify-center rounded border-0 bg-transparent transition hover:text-fg ${color}`}
    >
      {state === "copied" ? (
        <svg aria-hidden="true" viewBox="0 0 16 16" className="h-3.5 w-3.5 fill-current">
          <path d="M13.78 3.22a.75.75 0 0 1 0 1.06l-7.25 7.25a.75.75 0 0 1-1.06 0L2.22 8.28a.75.75 0 0 1 1.06-1.06L6 9.94l6.72-6.72a.75.75 0 0 1 1.06 0Z" />
        </svg>
      ) : (
        <svg aria-hidden="true" viewBox="0 0 16 16" className="h-3.5 w-3.5 fill-current">
          <path d="M5 1.75A1.75 1.75 0 0 1 6.75 0h5.5A1.75 1.75 0 0 1 14 1.75v5.5A1.75 1.75 0 0 1 12.25 9H11V7.5h1.25a.25.25 0 0 0 .25-.25v-5.5a.25.25 0 0 0-.25-.25h-5.5a.25.25 0 0 0-.25.25V3H5V1.75Z" />
          <path d="M2 4.75A1.75 1.75 0 0 1 3.75 3h5.5A1.75 1.75 0 0 1 11 4.75v5.5A1.75 1.75 0 0 1 9.25 12h-5.5A1.75 1.75 0 0 1 2 10.25v-5.5Zm1.75-.25a.25.25 0 0 0-.25.25v5.5c0 .138.112.25.25.25h5.5a.25.25 0 0 0 .25-.25v-5.5a.25.25 0 0 0-.25-.25h-5.5Z" />
        </svg>
      )}
    </button>
  );
}
