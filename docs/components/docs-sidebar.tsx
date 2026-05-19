"use client";

import Link from "next/link";
import { useState } from "react";
import { ChevronDownIcon, HamburgerIcon } from "@/components/icons";
import { Sheet, SheetTrigger, SheetContent, SheetTitle } from "@/components/ui/sheet";
import type { DocsGroup } from "@/lib/types";

type SidebarNavProps = {
  groups: DocsGroup[];
  activeSlug: string;
  onNavigate?: () => void;
};

function SidebarNav({ groups, activeSlug, onNavigate }: SidebarNavProps) {
  const initialCollapsed = new Set(
    groups
      .filter((g) => g.section !== "Learn" && !g.items.some((i) => i.slug === activeSlug))
      .map((g) => g.section),
  );
  const [collapsed, setCollapsed] = useState<Set<string>>(initialCollapsed);

  function toggle(section: string) {
    setCollapsed((prev) => {
      const next = new Set(prev);
      if (next.has(section)) next.delete(section);
      else next.add(section);
      return next;
    });
  }

  return (
    <nav className="px-3 py-4">
      {groups.map((group) => {
        const hasActive = group.items.some((item) => item.slug === activeSlug);
        const isCollapsed = collapsed.has(group.section) && !hasActive;
        return (
          <div key={group.section} className="mb-4">
            <button
              type="button"
              aria-expanded={!isCollapsed}
              onClick={() => toggle(group.section)}
              className="flex w-full cursor-pointer items-center justify-between rounded bg-transparent px-3 py-2 text-[0.6875rem] font-semibold uppercase tracking-[0.06em] text-muted transition hover:text-fg"
            >
              <span>{group.section}</span>
              <ChevronDownIcon className={`shrink-0 transition-transform ${isCollapsed ? "-rotate-90" : ""}`} />
            </button>
            <div
              className={`grid transition-[grid-template-rows] duration-200 ${
                isCollapsed ? "grid-rows-[0fr]" : "grid-rows-[1fr]"
              }`}
            >
              <div className="overflow-hidden">
                {group.items.map((item) => {
                  const active = item.slug === activeSlug;
                  return (
                    <Link
                      key={item.slug}
                      href={item.path}
                      aria-current={active ? "page" : undefined}
                      onClick={onNavigate}
                      className={`block rounded px-3 py-[0.1875rem] text-[0.8125rem] leading-[1.8] no-underline transition hover:text-fg ${
                        active ? "font-medium text-fg" : "text-muted"
                      }`}
                    >
                      {item.title}
                    </Link>
                  );
                })}
              </div>
            </div>
          </div>
        );
      })}
    </nav>
  );
}

type DocsSidebarShellProps = {
  groups: DocsGroup[];
  activeSlug: string;
  currentTitle?: string;
};

export function DocsSidebarShell({ groups, activeSlug, currentTitle }: DocsSidebarShellProps) {
  const [open, setOpen] = useState(false);

  return (
    <>
      <Sheet open={open} onOpenChange={setOpen}>
        <SheetTrigger
          aria-label="Open table of contents"
          className="sticky top-14 z-40 flex w-full items-center justify-between border-b border-border bg-bg/80 px-6 py-3 backdrop-blur-sm focus:outline-none md:hidden"
        >
          <span className="text-sm font-medium text-fg">{currentTitle ?? "Documentation"}</span>
          <span className="flex h-8 w-8 items-center justify-center text-muted">
            <HamburgerIcon className="h-4 w-4" />
          </span>
        </SheetTrigger>
        <SheetContent side="left" className="overflow-y-auto p-0" showCloseButton={false}>
          <SheetTitle className="px-6 pt-6">Table of Contents</SheetTitle>
          <SidebarNav groups={groups} activeSlug={activeSlug} onNavigate={() => setOpen(false)} />
        </SheetContent>
      </Sheet>

      <aside
        aria-label="Documentation"
        className="hidden md:sticky md:top-0 md:block md:h-screen md:w-60 md:overflow-y-auto"
      >
        <SidebarNav groups={groups} activeSlug={activeSlug} />
      </aside>
    </>
  );
}
