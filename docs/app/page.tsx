import { ArrowRightIcon, LogoIcon } from "@/components/icons";
import type { ReactNode } from "react";
import { ButtonLink } from "@/components/button";
import { InstallCopy } from "@/components/install-copy";
import { highlight } from "@/lib/highlight";
import { pageMetadata } from "@/lib/page-metadata";

export const metadata = pageMetadata("");

const CODE_EXAMPLE = `<span class="hl-keyword">pub</span> <span class="hl-keyword">fn</span> <span class="hl-variable">main</span>(world: <span class="hl-type">World</span>) -> <span class="hl-type">Void</span> <span class="hl-keyword">raises</span> {
    <span class="hl-keyword">check</span> world.out.<span class="hl-variable">write</span>(<span class="hl-string">"hello from zero\\n"</span>)
}`;

const GRAPH_EXAMPLE = `zero-graph v1
origin source-text
module "hello"
hash "graph:a7f7e6899a73f3b4"

node #decl_ad8d9028 Function name:"main" type:"Void" public:true fallible:true
node #param_4610ae76 Param name:"world" type:"World"
node #expr_c403020c MethodCall name:"write" type:"Void"
node #expr_653eeb6e Literal type:"String" value:"hello from zero\\n"
edge #expr_c403020c arg #expr_653eeb6e order:0`;

const PATCH_EXAMPLE = `zero patch \\
  --expect-graph-hash graph:a7f7e6899a73f3b4 \\
  --op 'set node="#expr_653eeb6e" \\
    field="value" \\
    expect="hello from zero\\n" \\
    value="hello graph\\n"'`;

const CONSTRAINTS = [
  "Token efficiency",
  "Low memory",
  "Fast startup",
  "Fast builds",
  "Low latency",
  "Zero dependencies",
];

/* ─── Primitives ──────────────────────────────────── */

function GridGlow() {
  return (
    <div aria-hidden className="pointer-events-none absolute inset-0 -z-10 overflow-hidden">
      <div
        className="absolute left-1/2 top-0 h-[42rem] w-[42rem] -translate-x-1/2 -translate-y-1/3 rounded-full opacity-[0.55] blur-[120px] dark:opacity-40"
        style={{
          background:
            "radial-gradient(circle, color-mix(in srgb, var(--color-fg) 14%, transparent), transparent 68%)",
        }}
      />
      <div
        className="absolute inset-0 opacity-[0.4] dark:opacity-[0.25]"
        style={{
          backgroundImage:
            "linear-gradient(to right, var(--color-border) 1px, transparent 1px), linear-gradient(to bottom, var(--color-border) 1px, transparent 1px)",
          backgroundSize: "64px 64px",
          maskImage:
            "radial-gradient(ellipse 80% 60% at 50% 0%, #000 30%, transparent 75%)",
          WebkitMaskImage:
            "radial-gradient(ellipse 80% 60% at 50% 0%, #000 30%, transparent 75%)",
        }}
      />
    </div>
  );
}

function SectionHeader({
  index,
  eyebrow,
  title,
  description,
}: {
  index: string;
  eyebrow: string;
  title: string;
  description: string;
}) {
  return (
    <div className="mb-12 max-w-[44rem]">
      <div className="mb-5 flex items-center gap-3 font-mono text-xs uppercase tracking-[0.18em] text-muted">
        <span>{index}</span>
        <span className="h-px w-8 bg-border" />
        <span>{eyebrow}</span>
      </div>
      <h2 className="m-0 max-w-[14ch] text-[clamp(2.25rem,6vw,4.25rem)] font-semibold leading-[0.98] tracking-[-0.045em]">
        {title}
      </h2>
      <p className="mt-6 max-w-[34rem] text-[1.0625rem] leading-[1.65] text-muted">
        {description}
      </p>
    </div>
  );
}

function Panel({ className = "", children }: { className?: string; children: ReactNode }) {
  return (
    <div
      className={`relative overflow-hidden rounded-2xl border border-border bg-surface ${className}`}
      style={{ boxShadow: "0 1px 0 0 color-mix(in srgb, var(--color-fg) 4%, transparent)" }}
    >
      {children}
    </div>
  );
}

function CodeWindow({
  title,
  html,
  className = "",
}: {
  title: string;
  html: string;
  className?: string;
}) {
  return (
    <Panel className={className}>
      <div className="flex items-center gap-3 border-b border-border px-4 py-3">
        <div className="flex gap-1.5">
          <span className="h-2.5 w-2.5 rounded-full bg-border" />
          <span className="h-2.5 w-2.5 rounded-full bg-border" />
          <span className="h-2.5 w-2.5 rounded-full bg-border" />
        </div>
        <span className="font-mono text-xs font-medium text-muted">{title}</span>
      </div>
      <pre className="m-0 overflow-x-auto bg-code-bg p-5 text-[0.8125rem] leading-[1.7]">
        <code className="text-code-fg" dangerouslySetInnerHTML={{ __html: html }} />
      </pre>
    </Panel>
  );
}

/* ─── Chat mockup ─────────────────────────────────── */

function CheckIcon() {
  return (
    <svg width="12" height="12" viewBox="0 0 16 16" fill="none" className="shrink-0 text-fg/60">
      <path
        d="M13.5 4.5 6.5 11.5 3 8"
        stroke="currentColor"
        strokeWidth="1.75"
        strokeLinecap="round"
        strokeLinejoin="round"
      />
    </svg>
  );
}

function ToolRun({ command }: { command: string }) {
  return (
    <div className="flex items-center gap-2 rounded-lg border border-border bg-surface px-3 py-2 font-mono text-[0.78125rem] leading-relaxed text-muted">
      <span className="select-none text-muted/60">›</span>
      <span className="min-w-0 flex-1 truncate text-fg/80">{command}</span>
      <CheckIcon />
    </div>
  );
}

function ChatMockup() {
  return (
    <Panel className="shadow-card">
      <div className="flex items-center gap-3 border-b border-border px-5 py-3.5">
        <span className="flex h-7 w-7 items-center justify-center rounded-full border border-border bg-bg">
          <LogoIcon width={12} height={10} className="text-fg" />
        </span>
        <div className="flex flex-col leading-tight">
          <span className="text-[0.8125rem] font-medium text-fg">Zero Agent</span>
          <span className="font-mono text-[0.6875rem] text-muted">graph session</span>
        </div>
        <span className="ml-auto flex items-center gap-1.5 font-mono text-[0.6875rem] text-muted">
          <span className="h-1.5 w-1.5 rounded-full bg-fg/40" />
          ready
        </span>
      </div>

      <div className="flex flex-col gap-5 p-5 sm:p-6">
        <div className="flex justify-end">
          <div className="max-w-[82%] rounded-2xl rounded-br-md bg-fg px-4 py-2.5 text-[0.875rem] leading-relaxed text-bg">
            Build me a hello world program in Zero.
          </div>
        </div>

        <div className="max-w-[92%] text-[0.875rem] leading-[1.65] text-fg">
          I&apos;ll set up a graph-first package, patch the graph, and run it. There are
          no source files to hand-write.
        </div>

        <div className="flex flex-col gap-2">
          <ToolRun command="zero init" />
          <ToolRun command={'zero patch --op addMain --op \'addCheckWrite fn="main" text="hello from zero\\n"\''} />
          <ToolRun command="zero run" />
        </div>

        <div className="max-w-[92%] text-[0.875rem] leading-[1.65] text-fg">
          Done. <code className="rounded bg-surface-muted px-1.5 py-0.5 font-mono text-[0.78125rem] text-fg">zero.graph</code> validated at{" "}
          <code className="rounded bg-surface-muted px-1.5 py-0.5 font-mono text-[0.78125rem] text-fg">graph:a7f7e689</code> with symbol{" "}
          <code className="rounded bg-surface-muted px-1.5 py-0.5 font-mono text-[0.78125rem] text-fg">main</code>. It prints:
        </div>

        <div className="rounded-lg border border-border bg-code-bg px-4 py-3 font-mono text-[0.8125rem] text-code-fg">
          hello from zero
        </div>
      </div>
    </Panel>
  );
}

/* ─── Section wrapper ─────────────────────────────── */

function Section({ children }: { children: ReactNode }) {
  return (
    <section className="relative z-10 mx-auto w-[min(100%-3rem,var(--container-content))] border-t border-border py-[clamp(5rem,11vh,8rem)]">
      {children}
    </section>
  );
}

export default function HomePage() {
  return (
    <div className="relative min-h-screen overflow-hidden">
      <main>
        {/* Hero */}
        <section className="relative overflow-hidden px-6 pb-24 pt-[clamp(4rem,11vh,7rem)]">
          <GridGlow />
          <div className="relative z-10 mx-auto flex max-w-[54rem] flex-col items-center text-center">
            <div className="mb-7 inline-flex items-center rounded-full border border-border bg-surface/70 px-3.5 py-1 font-mono text-[0.6875rem] uppercase tracking-[0.16em] text-muted backdrop-blur">
              Experimental
            </div>
            <h1 className="m-0 text-[clamp(2.5rem,7vw,5rem)] font-semibold leading-[0.98] tracking-[-0.05em]">
              The future of
              <br />
              programming is a graph.
            </h1>
            <p className="mt-7 max-w-[40rem] text-[clamp(1.0625rem,2vw,1.25rem)] leading-[1.6] text-muted">
              Zero is the programming language for agents. They reason over semantic
              program structure and submit checked graph edits, instead of guessing at
              raw source text.
            </p>
            <InstallCopy />
            <p className="mt-5 max-w-[34rem] text-[0.8125rem] leading-relaxed text-muted">
              Expect breaking changes. Run it in a safe environment, not against
              production systems.
            </p>
          </div>
        </section>

        {/* 01 — Chat (Why) */}
        <Section>
          <SectionHeader
            index="01"
            eyebrow="How you use it"
            title="You just ask."
            description="Humans use chat. Nobody hand-writes Zero. You describe what you want, and the agent initializes a package, patches the graph, and runs it."
          />
          <div className="mx-auto max-w-[40rem]">
            <ChatMockup />
          </div>
        </Section>

        {/* 02 — Graph (What) */}
        <Section>
          <SectionHeader
            index="02"
            eyebrow="What it is"
            title="The graph is the program."
            description="Source text is a weak interface for understanding code. Zero keeps a typed program graph as the real artifact. Readable source is just a projection of it."
          />
          <div className="grid grid-cols-1 gap-5 lg:grid-cols-2">
            <CodeWindow title="src/main.0 — projection" html={CODE_EXAMPLE} />
            <CodeWindow title="zero query — graph" html={highlight(GRAPH_EXAMPLE, "zero-graph")} />
          </div>
        </Section>

        {/* 03 — Patch (How) */}
        <Section>
          <SectionHeader
            index="03"
            eyebrow="How edits land"
            title="Every edit is checked."
            description="Graph patches target a semantic node and field, guarded by a graph-hash and an expected value. Stale edits fail before they touch anything."
          />
          <div className="grid grid-cols-1 gap-5 lg:grid-cols-[1.4fr_1fr]">
            <CodeWindow title="zero patch" html={highlight(PATCH_EXAMPLE, "sh")} />
            <Panel className="bg-code-bg">
              <div className="flex items-center gap-3 border-b border-border bg-surface px-4 py-3">
                <span className="flex h-4 w-4 items-center justify-center rounded-full border border-border">
                  <CheckIcon />
                </span>
                <span className="font-mono text-xs font-medium text-muted">validated</span>
              </div>
              <dl className="m-0 divide-y divide-border font-mono text-[0.8125rem]">
                {[
                  ["graph hash", "graph:b3c1d04f"],
                  ["node", "#expr_653eeb6e"],
                  ["field", "value"],
                  ["symbols", "main"],
                  ["projection", "src/main.0"],
                ].map(([k, v]) => (
                  <div key={k} className="flex items-center justify-between gap-4 px-5 py-3">
                    <dt className="text-muted">{k}</dt>
                    <dd className="m-0 truncate text-code-fg">{v}</dd>
                  </div>
                ))}
              </dl>
            </Panel>
          </div>
        </Section>

        {/* 04 — Constraints */}
        <Section>
          <SectionHeader
            index="04"
            eyebrow="Under the hood"
            title="Still a systems language."
            description="The graph work does not relax the runtime goals. Zero is designed to stay small, fast, and dependency-free for both humans and the agents writing it."
          />
          <div className="grid grid-cols-2 gap-px overflow-hidden rounded-2xl border border-border bg-border md:grid-cols-3">
            {CONSTRAINTS.map((c) => (
              <div
                key={c}
                className="flex items-center gap-3 bg-bg px-6 py-7 text-[0.9375rem] font-medium tracking-[-0.01em] transition-colors hover:bg-surface-muted"
              >
                <span className="h-1.5 w-1.5 shrink-0 rounded-full bg-fg/30" />
                {c}
              </div>
            ))}
          </div>
        </Section>

        {/* CTA */}
        <section className="relative z-10 overflow-hidden border-t border-border">
          <div
            aria-hidden
            className="pointer-events-none absolute inset-x-0 bottom-0 -z-10 h-full"
            style={{
              background:
                "radial-gradient(ellipse 60% 100% at 50% 120%, color-mix(in srgb, var(--color-fg) 10%, transparent), transparent 70%)",
            }}
          />
          <div className="mx-auto flex w-[min(100%-3rem,var(--container-content))] flex-col items-center px-6 py-[clamp(6rem,14vh,9rem)] text-center">
            <h2 className="m-0 text-[clamp(2rem,6vw,3.5rem)] font-semibold leading-[1.02] tracking-[-0.045em]">
              Explore with us.
            </h2>
            <p className="mx-auto mt-6 max-w-[38rem] text-[1.0625rem] leading-[1.6] text-muted">
              Install the compiler, ask an agent for a program, inspect the graph, and
              test the edit loop. The most useful feedback is what helps agents work
              with less guessing.
            </p>
            <div className="mt-9 flex flex-wrap items-center justify-center gap-3">
              <ButtonLink href="/getting-started" variant="primary" size="lg">
                Get started <ArrowRightIcon />
              </ButtonLink>
            </div>
          </div>
        </section>
      </main>

      <footer className="relative z-10 border-t border-border py-8">
        <div className="mx-auto flex w-[min(100%-3rem,var(--container-content))] flex-col items-center gap-2 text-center md:flex-row md:justify-between md:text-left">
          <div className="flex items-center gap-2 text-sm font-semibold text-fg">
            <LogoIcon width="14" height="12" />
            <span>zerolang</span>
          </div>
          <p className="m-0 text-[0.8125rem] text-muted">
            Experimental graph-first language design.
          </p>
        </div>
      </footer>
    </div>
  );
}
