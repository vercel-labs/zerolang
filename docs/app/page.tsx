import { ArrowRightIcon, LogoIcon } from "@/components/icons";
import type { ReactNode } from "react";
import { ButtonLink } from "@/components/button";
import { InstallCopy } from "@/components/install-copy";
import { pageMetadata } from "@/lib/page-metadata";

export const metadata = pageMetadata("");

const PILLARS = [
  {
    metric: "Learnable",
    label: "on demand",
    title: "Small surface area",
    description:
      "zerolang is aiming for a language an agent can learn while working: regular syntax, few special cases, and compiler feedback that points toward the next edit.",
  },
  {
    metric: "Library",
    label: "first",
    title: "Fewer dependency searches",
    description:
      "The long-term goal is a standard library broad and consistent enough that most programs start with documented APIs, not package selection.",
  },
  {
    metric: "Inspectable",
    label: "by tools",
    title: "Deterministic repair loops",
    description:
      "The toolchain is intended to expose diagnostics, graphs, size reports, explanations, and repair plans as structured data agents can consume.",
  },
];

const FEATURES = [
  { title: "Pre-1 by design", description: "Today's syntax and APIs are not a contract. Breaking changes are expected while zerolang searches for what works best for agents." },
  { title: "Safe environments only", description: "Security vulnerabilities should be expected. Run and develop zerolang in isolated environments, not production systems or sensitive infrastructure." },
  { title: "Exploration over mastery", description: "Try the current shape, inspect the output, and send feedback. The details will move as the experiment learns." },
  { title: "One obvious path", description: "The language should favor a small set of regular patterns over many interchangeable styles." },
  { title: "Standard library over sugar", description: "New capability should usually live in documented APIs before it becomes new syntax." },
  { title: "Agent-readable tooling", description: "Diagnostics, graph facts, size reports, and repair metadata should be available as structured output." },
  { title: "Explicit effects", description: "Outside-world access, fallibility, and resource use should stay visible to both readers and tools." },
  { title: "No legacy promises", description: "When a clearer agent-facing design wins, zerolang can replace old behavior instead of carrying compatibility paths forward." },
  { title: "DX as a goal", description: "Checking, inspecting, explaining, and repairing code should feel direct even when the language is intentionally explicit." },
];

const CODE_EXAMPLE = `<span class="hl-keyword">fn</span> <span class="hl-variable">answer</span> <span class="hl-type">i32</span>
  <span class="hl-keyword">ret</span> + <span class="hl-number">40</span> <span class="hl-number">2</span>

<span class="hl-keyword">pub</span> <span class="hl-keyword">fn</span> <span class="hl-variable">main</span> <span class="hl-type">Void</span> world <span class="hl-type">World</span> <span class="hl-keyword">!</span>
  <span class="hl-keyword">if</span> == <span class="hl-variable">answer</span>() <span class="hl-number">42</span>
    <span class="hl-keyword">check</span> world.out.<span class="hl-variable">write</span> <span class="hl-string">"math works\\n"</span>`;

function CodeWindow({ title, html, children }: { title: string; html?: string; children?: ReactNode }) {
  return (
    <div className="overflow-hidden rounded-lg border border-border bg-code-bg shadow-card">
      <div className="flex items-center gap-3 border-b border-border bg-surface px-4 py-3">
        <div className="flex gap-1.5">
          <span className="h-2.5 w-2.5 rounded-full bg-border" />
          <span className="h-2.5 w-2.5 rounded-full bg-border" />
          <span className="h-2.5 w-2.5 rounded-full bg-border" />
        </div>
        <span className="font-mono text-xs font-medium text-muted">{title}</span>
      </div>
      <pre className="m-0 overflow-x-auto p-6 text-sm leading-7">
        <code className="text-code-fg">
          {html ? <span dangerouslySetInnerHTML={{ __html: html }} /> : children}
        </code>
      </pre>
    </div>
  );
}

export default function HomePage() {
  return (
    <div className="relative min-h-screen overflow-hidden">
      <main>
        <section className="relative z-10 mx-auto flex max-w-[52rem] flex-col items-center px-6 pb-16 pt-[clamp(4rem,12vh,8rem)] text-center">
          <div className="mb-6 inline-flex items-center rounded-full border border-border bg-surface px-3 py-1 text-[0.8125rem] font-medium text-muted">
            Pre-1 experiment
          </div>
          <h1 className="m-0 text-[clamp(2rem,5vw,3.75rem)] font-bold leading-[1.15] tracking-[-0.045em]">
            The programming language
            <br />
            for agents
          </h1>
          <p className="mt-6 max-w-[38rem] text-[clamp(1rem,2vw,1.1875rem)] leading-[1.65] text-muted">
            zerolang explores what a programming language can look like when agents
            are primary users from day one. The aim is a language that is easy to
            learn on the fly, deterministic to inspect and repair, standard-library
            first, and explicit enough that most tasks have one obvious path.
          </p>
          <InstallCopy />
          <p className="mt-4 max-w-[34rem] text-sm leading-relaxed text-muted">
            The current toolchain is useful for exploration, but today's syntax
            and APIs are not a contract. Expect breaking changes while zerolang
            searches for what works best for agents. Run it in a safe
            environment, not against production systems.
          </p>
        </section>

        <section className="relative z-10 mx-auto grid w-[min(100%-3rem,var(--container-content))] grid-cols-1 gap-px overflow-hidden rounded-lg border border-border bg-border md:grid-cols-3">
          {PILLARS.map((p) => (
            <div key={p.title} className="flex flex-col gap-3 bg-bg p-8">
              <div className="mb-2 flex flex-col gap-0.5">
                <span className="font-mono text-2xl font-bold tracking-[-0.03em] text-fg">{p.metric}</span>
                <span className="text-xs font-medium uppercase tracking-[0.04em] text-muted">{p.label}</span>
              </div>
              <h3 className="m-0 text-base font-semibold tracking-[-0.01em]">{p.title}</h3>
              <p className="m-0 text-sm leading-relaxed text-muted">{p.description}</p>
            </div>
          ))}
        </section>

        <section className="relative z-10 mx-auto w-[min(100%-3rem,var(--container-content))] border-t border-border py-[clamp(4rem,8vh,6rem)]">
          <div className="grid grid-cols-1 gap-6 lg:grid-cols-2">
            <CodeWindow title="main.0" html={CODE_EXAMPLE} />
            <CodeWindow title="zero check">
              <span className="text-[#D73A49] dark:text-[#F97583]">$</span>
              <span className="text-[#6F42C1] dark:text-[#B392F0]"> zero</span>
              <span className="text-[#032F62] dark:text-[#9ECBFF]"> check examples/hello.0</span>
              {"\nhello.0:1:4 PAR100: expected '{' before block\n  explain: zero explain PAR100"}
            </CodeWindow>
          </div>
        </section>

        <section className="relative z-10 mx-auto w-[min(100%-3rem,var(--container-content))] border-t border-border py-[clamp(4rem,8vh,6rem)]">
          <div className="mx-auto mb-12 max-w-[36rem] text-center">
            <p className="mb-2 text-[0.8125rem] font-semibold uppercase tracking-[0.04em] text-blue">Direction</p>
            <h2 className="mb-4 text-[clamp(1.5rem,4vw,2.25rem)] font-bold leading-[1.15] tracking-[-0.035em]">Regularity over cleverness.</h2>
            <p className="m-0 leading-[1.65] text-muted">
              zerolang favors explicit capabilities and standard-library APIs over
              syntax for every convenience. Some code may be more verbose for
              humans if that makes it easier for agents to generate, inspect,
              and repair.
            </p>
          </div>
          <div className="grid grid-cols-1 gap-px overflow-hidden rounded-lg border border-border bg-border md:grid-cols-2 lg:grid-cols-3">
            {FEATURES.map((f) => (
              <div key={f.title} className="flex flex-col gap-3 bg-bg p-8 transition hover:bg-surface-muted">
                <h3 className="m-0 text-[0.9375rem] font-semibold tracking-[-0.01em]">{f.title}</h3>
                <p className="m-0 text-sm leading-relaxed text-muted">{f.description}</p>
              </div>
            ))}
          </div>
        </section>

        <section className="relative z-10 border-t border-border px-6 py-[clamp(5rem,12vh,8rem)] text-center">
          <h2 className="mb-3 text-[clamp(1.75rem,5vw,2.75rem)] font-bold leading-[1.1] tracking-[-0.04em]">Explore with us.</h2>
          <p className="mx-auto mb-8 max-w-[32rem] text-[1.0625rem] leading-relaxed text-muted">
            Install the compiler, run an example, and inspect what the experiment
            can do today. The most useful feedback is what helps agents work
            with less guesswork.
          </p>
          <div className="flex flex-wrap items-center justify-center gap-3">
            <ButtonLink href="/getting-started" variant="primary" size="lg">
              Get started <ArrowRightIcon />
            </ButtonLink>
          </div>
        </section>
      </main>

      <footer className="relative z-10 border-t border-border py-8">
        <div className="mx-auto flex w-[min(100%-3rem,var(--container-content))] flex-col items-center gap-2 text-center md:flex-row md:justify-between md:text-left">
          <div className="flex items-center gap-2 text-sm font-semibold text-fg">
            <LogoIcon width="14" height="12" />
            <span>zerolang</span>
          </div>
          <p className="m-0 text-[0.8125rem] text-muted">Agent-first language design, still under active exploration.</p>
        </div>
      </footer>
    </div>
  );
}
