"use client";

import type { KeyboardEvent } from "react";
import { useCallback, useEffect, useRef, useState } from "react";
import { useRouter } from "next/navigation";
import { Dialog, DialogContent, DialogTitle } from "@/components/ui/dialog";
import { cn } from "@/lib/utils";
import type { SearchResult } from "@/lib/types";

export function Search() {
  const router = useRouter();
  const [open, setOpen] = useState(false);
  const [query, setQuery] = useState("");
  const [results, setResults] = useState<SearchResult[]>([]);
  const [loading, setLoading] = useState(false);
  const [activeIndex, setActiveIndex] = useState(0);
  const inputRef = useRef<HTMLInputElement | null>(null);
  const listRef = useRef<HTMLDivElement | null>(null);
  const abortRef = useRef<AbortController | null>(null);

  const navigate = useCallback(
    (href: string) => {
      setOpen(false);
      setQuery("");
      setResults([]);
      router.push(href);
    },
    [router],
  );

  useEffect(() => {
    function onKeyDown(e: globalThis.KeyboardEvent) {
      if ((e.metaKey || e.ctrlKey) && e.key === "k") {
        e.preventDefault();
        setOpen((prev) => !prev);
      }
    }
    document.addEventListener("keydown", onKeyDown);
    return () => document.removeEventListener("keydown", onKeyDown);
  }, []);

  useEffect(() => {
    if (open) {
      setTimeout(() => inputRef.current?.focus(), 0);
    } else {
      setQuery("");
      setResults([]);
    }
  }, [open]);

  useEffect(() => {
    const q = query.trim();
    if (!q) {
      setResults([]);
      setLoading(false);
      return;
    }

    setLoading(true);
    abortRef.current?.abort();
    const controller = new AbortController();
    abortRef.current = controller;

    const timeout = setTimeout(async () => {
      try {
        const res = await fetch(`/api/search?q=${encodeURIComponent(q)}`, { signal: controller.signal });
        if (res.ok) {
          const data = (await res.json()) as { results?: SearchResult[] };
          setResults(Array.isArray(data.results) ? data.results : []);
        }
      } catch {
        // aborted or network error
      } finally {
        if (!controller.signal.aborted) setLoading(false);
      }
    }, 150);

    return () => {
      clearTimeout(timeout);
      controller.abort();
    };
  }, [query]);

  useEffect(() => {
    setActiveIndex(0);
  }, [results]);

  function handleKeyDown(e: KeyboardEvent<HTMLInputElement>) {
    if (e.key === "ArrowDown") {
      e.preventDefault();
      setActiveIndex((i) => Math.min(i + 1, results.length - 1));
    } else if (e.key === "ArrowUp") {
      e.preventDefault();
      setActiveIndex((i) => Math.max(i - 1, 0));
    } else if (e.key === "Enter" && results[activeIndex]) {
      e.preventDefault();
      navigate(results[activeIndex].href);
    }
  }

  useEffect(() => {
    const active = listRef.current?.querySelector("[data-active='true']");
    active?.scrollIntoView({ block: "nearest" });
  }, [activeIndex]);

  const hasQuery = query.trim().length > 0;

  return (
    <>
      <button
        onClick={() => setOpen(true)}
        className="hidden cursor-pointer items-center gap-2 rounded-md border border-border bg-surface-muted px-3 py-1.5 text-sm text-muted transition-colors hover:border-fg/25 hover:text-fg sm:flex"
      >
        <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
          <circle cx="11" cy="11" r="8" />
          <path d="m21 21-4.3-4.3" />
        </svg>
        Search docs
        <kbd className="pointer-events-none ml-1 inline-flex items-center gap-0.5 rounded border border-border bg-bg px-1.5 py-0.5 font-mono text-[10px] text-muted">
          <span>&#8984;</span>K
        </kbd>
      </button>

      <button
        onClick={() => setOpen(true)}
        className="flex cursor-pointer items-center text-muted transition-colors hover:text-fg sm:hidden"
        aria-label="Search docs"
      >
        <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
          <circle cx="11" cy="11" r="8" />
          <path d="m21 21-4.3-4.3" />
        </svg>
      </button>

      <Dialog open={open} onOpenChange={setOpen}>
        <DialogContent showCloseButton={false} className="gap-0 p-0 sm:max-w-lg">
          <DialogTitle className="sr-only">Search documentation</DialogTitle>
          <div className="flex items-center gap-2 border-b border-border px-3">
            <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round" className="shrink-0 text-muted">
              <circle cx="11" cy="11" r="8" />
              <path d="m21 21-4.3-4.3" />
            </svg>
            <input
              ref={inputRef}
              value={query}
              onChange={(e) => setQuery(e.target.value)}
              onKeyDown={handleKeyDown}
              placeholder="Search docs..."
              className="flex-1 bg-transparent py-3 text-sm outline-none placeholder:text-muted"
            />
            {query && (
              <button onClick={() => setQuery("")} className="text-muted hover:text-fg">
                <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
                  <path d="M18 6 6 18" />
                  <path d="m6 6 12 12" />
                </svg>
              </button>
            )}
          </div>

          <div ref={listRef} className="max-h-[min(60vh,400px)] overflow-y-auto p-2">
            {loading && hasQuery ? (
              <div className="flex items-center justify-center py-6">
                <div className="h-4 w-4 animate-spin rounded-full border-2 border-muted border-t-transparent" />
              </div>
            ) : hasQuery && results.length === 0 ? (
              <p className="py-6 text-center text-sm text-muted">No results found.</p>
            ) : !hasQuery ? (
              <p className="py-6 text-center text-sm text-muted">Type to search documentation...</p>
            ) : (
              results.map((item, i) => (
                <button
                  key={item.href}
                  data-active={i === activeIndex}
                  onClick={() => navigate(item.href)}
                  onMouseEnter={() => setActiveIndex(i)}
                  className={cn(
                    "flex w-full flex-col gap-1 rounded-md px-3 py-2 text-left transition-colors",
                    i === activeIndex ? "bg-surface-muted text-fg" : "text-fg",
                  )}
                >
                  <span className="text-sm font-medium">{item.title}</span>
                  {item.snippet && (
                    <span className="line-clamp-2 text-xs leading-relaxed text-muted">{item.snippet}</span>
                  )}
                </button>
              ))
            )}
          </div>
        </DialogContent>
      </Dialog>
    </>
  );
}
