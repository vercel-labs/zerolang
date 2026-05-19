import Link from "next/link";
import { GeistPixelSquare } from "geist/font/pixel";
import { Search } from "@/components/search";
import { ThemeToggle } from "@/components/theme-toggle";
import { LogoIcon } from "@/components/icons";

export function SiteHeader({ stars }: { stars: string }) {
  return (
    <header className="sticky top-0 z-50 bg-white/90 backdrop-blur-sm dark:bg-neutral-950/90">
      <div className="flex h-14 items-center justify-between gap-6 px-4">
        <div className="flex items-center gap-2">
          <Link href="/" className="flex items-center gap-2">
            <LogoIcon width="16" height="14" />
            <span className={`${GeistPixelSquare.className} text-lg`}>zero</span>
          </Link>
        </div>

        <nav className="flex items-center gap-4">
          <Link
            href="/getting-started"
            className="text-sm text-neutral-500 transition-colors hover:text-neutral-900 dark:text-neutral-400 dark:hover:text-neutral-100"
          >
            Docs
          </Link>
          <Search />
          <a
            href={`https://github.com/${process.env.NEXT_PUBLIC_GITHUB_REPO || "vercel-labs/zero"}`}
            target="_blank"
            rel="noopener noreferrer"
            className="flex items-center gap-1.5 text-sm text-neutral-500 transition-colors hover:text-neutral-900 dark:text-neutral-400 dark:hover:text-neutral-100"
          >
            <svg viewBox="0 0 16 16" className="h-4 w-4" fill="currentColor" aria-hidden="true">
              <path d="M8 0C3.58 0 0 3.58 0 8c0 3.54 2.29 6.53 5.47 7.59.4.07.55-.17.55-.38 0-.19-.01-.82-.01-1.49-2.01.37-2.53-.49-2.69-.94-.09-.23-.48-.94-.82-1.13-.28-.15-.68-.52-.01-.53.63-.01 1.08.58 1.23.82.72 1.21 1.87.87 2.33.66.07-.52.28-.87.51-1.07-1.78-.2-3.64-.89-3.64-3.95 0-.87.31-1.59.82-2.15-.08-.2-.36-1.02.08-2.12 0 0 .67-.21 2.2.82.64-.18 1.32-.27 2-.27.68 0 1.36.09 2 .27 1.53-1.04 2.2-.82 2.2-.82.44 1.1.16 1.92.08 2.12.51.56.82 1.27.82 2.15 0 3.07-1.87 3.75-3.65 3.95.29.25.54.73.54 1.48 0 1.07-.01 1.93-.01 2.2 0 .21.15.46.55.38A8.013 8.013 0 0016 8c0-4.42-3.58-8-8-8z" />
            </svg>
            {stars ? <span>{stars}</span> : null}
          </a>
          <ThemeToggle />
        </nav>
      </div>
    </header>
  );
}
