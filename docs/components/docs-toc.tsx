"use client";

import { useEffect, useRef, useState } from "react";
import type { Heading } from "@/lib/types";

export function DocsToc({ headings }: { headings: Heading[] }) {
  const [activeId, setActiveId] = useState<string | null>(headings[0]?.id ?? null);
  const observerRef = useRef<IntersectionObserver | null>(null);

  useEffect(() => {
    if (typeof window === "undefined" || headings.length === 0) return;

    const ids = headings.map((h) => h.id);
    const elements = ids
      .map((id) => document.getElementById(id))
      .filter((el): el is HTMLElement => el !== null);
    if (elements.length === 0) return;

    const visible = new Set();

    const observer = new IntersectionObserver(
      (entries) => {
        for (const entry of entries) {
          if (entry.isIntersecting) visible.add(entry.target.id);
          else visible.delete(entry.target.id);
        }

        let next = null;
        for (const h of headings) {
          if (visible.has(h.id)) {
            next = h.id;
            break;
          }
        }
        if (!next && visible.size === 0) {
          const scrollY = window.scrollY;
          for (let i = elements.length - 1; i >= 0; i--) {
            if (elements[i].offsetTop <= scrollY + 80) {
              next = elements[i].id;
              break;
            }
          }
        }
        if (next) setActiveId(next);
      },
      { rootMargin: "-64px 0px -60% 0px", threshold: 0 },
    );

    elements.forEach((el) => observer.observe(el));
    observerRef.current = observer;

    return () => observer.disconnect();
  }, [headings]);

  if (headings.length === 0) return null;

  return (
    <aside
      aria-label="On this page"
      className="sticky top-0 hidden h-screen overflow-y-auto py-8 pl-0 pr-4 lg:block"
    >
      <p className="mb-3 pl-4 text-xs font-semibold uppercase tracking-[0.05em] text-fg">
        On this page
      </p>
      <nav>
        {headings.map((h) => {
          const indent = h.level === 3 ? "pl-7" : h.level === 4 ? "pl-10" : "pl-4";
          return (
            <a
              key={h.id}
              href={`#${h.id}`}
              className={`block py-1 pr-4 ${indent} text-[0.8125rem] leading-snug no-underline transition hover:text-fg ${
                activeId === h.id ? "text-fg" : "text-muted"
              }`}
            >
              {h.text}
            </a>
          );
        })}
      </nav>
    </aside>
  );
}
