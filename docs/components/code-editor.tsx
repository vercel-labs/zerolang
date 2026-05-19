"use client";

import type { UIEvent } from "react";
import { useMemo, useRef } from "react";
import { highlight } from "@/lib/highlight";

type CodeEditorProps = {
  value: string;
  onChange: (value: string) => void;
  language?: string;
  ariaLabel: string;
  className?: string;
};

export function CodeEditor({
  value,
  onChange,
  language = "zero",
  ariaLabel,
  className = "",
}: CodeEditorProps) {
  const preRef = useRef<HTMLPreElement | null>(null);

  const html = useMemo(() => {
    const trailingNewline = value.endsWith("\n") ? " " : "";
    return highlight(value + trailingNewline, language);
  }, [value, language]);

  function handleScroll(event: UIEvent<HTMLTextAreaElement>) {
    const pre = preRef.current;
    if (!pre) return;
    pre.scrollTop = event.currentTarget.scrollTop;
    pre.scrollLeft = event.currentTarget.scrollLeft;
  }

  return (
    <div className={`relative h-full w-full ${className}`}>
      <pre
        ref={preRef}
        aria-hidden="true"
        className="pointer-events-none absolute inset-0 m-0 overflow-hidden whitespace-pre p-6 font-mono text-[0.9375rem] leading-[1.7] text-code-fg"
      >
        <code dangerouslySetInnerHTML={{ __html: html }} />
      </pre>
      <textarea
        value={value}
        onChange={(event) => onChange(event.target.value)}
        onScroll={handleScroll}
        spellCheck={false}
        autoCorrect="off"
        autoCapitalize="off"
        autoComplete="off"
        aria-label={ariaLabel}
        className="relative block h-full w-full resize-none whitespace-pre border-0 bg-transparent p-6 font-mono text-[0.9375rem] leading-[1.7] text-transparent caret-fg outline-none"
      />
    </div>
  );
}
