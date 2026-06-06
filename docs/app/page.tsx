import { ArrowRightIcon, LogoIcon } from "@/components/icons";
import type { ReactNode } from "react";
import { ButtonLink } from "@/components/button";
import { InstallCopy } from "@/components/install-copy";
import { pageMetadata } from "@/lib/page-metadata";

export const metadata = pageMetadata("");

const PILLARS = [
  {
    metric: "Graph",
    label: "first",
    title: "Semantic edit surface",
    description:
      "Agents can inspect checked ProgramGraph facts and submit graph edits instead of only patching source text ranges.",
  },
  {
    metric: ".0",
    label: "view",
    title: "Human-readable projection",
    description:
      ".0 source stays reviewable and bidirectional for humans, while graph-first packages compile from zero.graph.",
  },
  {
    metric: "Small",
    label: "systems",
    title: "Tight runtime goals",
    description:
      "Token efficiency, low memory usage, fast startup, fast builds, low runtime latency, and zero dependencies remain design constraints.",
  },
];

const FEATURES = [
  { title: "Experimental by design", description: "Today's syntax and APIs are not a contract. Breaking changes and removed compatibility paths are expected when a clearer agent-facing design wins." },
  { title: "Safe environments only", description: "Security vulnerabilities should be expected. Run and develop zerolang in isolated environments, not production systems or sensitive infrastructure." },
  { title: "Repository graph store", description: "Graph-first packages keep zero.graph as compiler input. .0 files are projections, and ProgramGraph artifacts are optional inspection data." },
  { title: "Checked graph edits", description: "Graph patches can target semantic nodes with graph-hash and field-value preconditions before projections are refreshed." },
  { title: "Semantic facts", description: "The compiler can expose node IDs, resolved types, effects, ownership facts, capabilities, helper use, and module edges." },
  { title: "Regular source", description: "The syntax should stay boring enough to index, compare, format, audit, and regenerate while still reading like normal code." },
  { title: "Version-matched skills", description: "The compiler ships language, graph, diagnostics, build, testing, package, and stdlib guides that match the binary in use." },
  { title: "Explicit effects", description: "Outside-world access, fallibility, ownership, and resource use should stay visible to both readers and tools." },
  { title: "Direct CLI surface", description: "Agents can use compiler commands for checks, graph inspection, patching, size reports, explanations, and repair plans." },
];

const WHY_GRAPH = [
  {
    title: "Semantic navigation",
    description:
      "Agents should be able to start from a symbol, diagnostic, call, capability, module, or node ID and gather the relevant semantic slice without reading the whole file.",
  },
  {
    title: "Precise edits",
    description:
      "Graph edits target compiler nodes and fields, with graph-hash and expected-value checks, instead of relying only on line ranges or matching source text.",
  },
  {
    title: "Validated refactors",
    description:
      "Refactors can be represented as operations on resolved program structure: rename this function node, replace this resolved callee, update these related references.",
  },
  {
    title: "Shorter feedback loop",
    description:
      "A graph patch can validate, lower, write, format, reparse, and check through the compiler instead of leaving agents to chain text edits and cleanup commands.",
  },
];

const CODE_EXAMPLE = `<span class="hl-keyword">fn</span> <span class="hl-variable">answer</span>() -> <span class="hl-type">i32</span> {
    <span class="hl-keyword">return</span> <span class="hl-number">40</span> + <span class="hl-number">2</span>
}

<span class="hl-keyword">pub</span> <span class="hl-keyword">fn</span> <span class="hl-variable">main</span>(world: <span class="hl-type">World</span>) -> <span class="hl-type">Void</span> <span class="hl-keyword">raises</span> {
    <span class="hl-keyword">if</span> <span class="hl-variable">answer</span>() == <span class="hl-number">42</span> {
        <span class="hl-keyword">check</span> world.out.<span class="hl-variable">write</span>(<span class="hl-string">"math works\\n"</span>)
    }
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

const PATCH_EXAMPLE = `zero patch examples/hello.0 \\
  --expect-graph-hash graph:a7f7e6899a73f3b4 \\
  --op 'set node="#expr_653eeb6e" field="value" expect="hello from zero\\n" value="hello graph\\n"'`;

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
            Experimental
          </div>
          <h1 className="m-0 text-[clamp(2rem,5vw,3.75rem)] font-bold leading-[1.15] tracking-[-0.045em]">
            The programming language
            <br />
            for agents
          </h1>
          <p className="mt-6 max-w-[42rem] text-[clamp(1rem,2vw,1.1875rem)] leading-[1.65] text-muted">
            zerolang is an experimental graph-first programming language where agents work with semantic program structure instead of raw source text.
          </p>
          <InstallCopy />
          <p className="mt-4 max-w-[36rem] text-sm leading-relaxed text-muted">
            Expect breaking changes. Run it in a safe environment, not against production systems.
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
          <div className="mx-auto mb-10 max-w-[48rem] text-center">
            <p className="mb-2 text-[0.8125rem] font-semibold uppercase tracking-[0.04em] text-blue">Why graph</p>
            <h2 className="mb-4 text-[clamp(1.5rem,4vw,2.25rem)] font-bold leading-[1.15] tracking-[-0.035em]">
              Graph is the artifact.
              <br />
              Source is the projection.
            </h2>
            <p className="m-0 text-[1.0625rem] leading-[1.65] text-muted">
              Source text is good for humans and review, but it is a weak interface for program understanding. Agents need to gather focused context, know what a call resolves to, avoid stale edits, change related structure, and get validation before projections are refreshed.
            </p>
          </div>
          <div className="grid grid-cols-1 gap-px overflow-hidden rounded-lg border border-border bg-border md:grid-cols-2">
            {WHY_GRAPH.map((item) => (
              <div key={item.title} className="bg-bg p-8">
                <h3 className="m-0 text-base font-semibold tracking-[-0.01em]">{item.title}</h3>
                <p className="mt-3 mb-0 text-sm leading-relaxed text-muted">{item.description}</p>
              </div>
            ))}
          </div>
        </section>

        <section className="relative z-10 mx-auto w-[min(100%-3rem,var(--container-content))] border-t border-border py-[clamp(4rem,8vh,6rem)]">
          <div className="grid grid-cols-1 gap-6 lg:grid-cols-2">
            <CodeWindow title="main.0 projection" html={CODE_EXAMPLE} />
            <CodeWindow title="zero graph dump">
              {GRAPH_EXAMPLE}
            </CodeWindow>
          </div>
          <div className="mt-6">
            <CodeWindow title="zero patch">
              {PATCH_EXAMPLE}
            </CodeWindow>
          </div>
        </section>

        <section className="relative z-10 mx-auto w-[min(100%-3rem,var(--container-content))] border-t border-border py-[clamp(4rem,8vh,6rem)]">
          <div className="mx-auto mb-12 max-w-[48rem] text-center">
            <p className="mb-2 text-[0.8125rem] font-semibold uppercase tracking-[0.04em] text-blue">Direction</p>
            <h2 className="mb-4 text-[clamp(1.5rem,4vw,2.25rem)] font-bold leading-[1.15] tracking-[-0.035em]">Graph as data. Source as projection.</h2>
            <p className="m-0 leading-[1.65] text-muted">
              The language is still designed under systems constraints: token efficiency, low memory usage, fast startup, fast builds, low runtime latency, and zero dependencies. The graph work does not remove readable source; it makes source the human projection of a checked structure and gives agents a tighter write, validate, check, and sync loop.
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
          <p className="mx-auto mb-8 max-w-[36rem] text-[1.0625rem] leading-relaxed text-muted">
            Install the compiler, run an example, inspect the graph, and test the edit loop. The most useful feedback is what helps agents work with less guessing.
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
          <p className="m-0 text-[0.8125rem] text-muted">Experimental graph-first language design.</p>
        </div>
      </footer>
    </div>
  );
}
